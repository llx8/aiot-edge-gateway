#include "HttpDashboard.h"
#include "ShmReader.h"
#include "Logger.h"

#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <cstring>
#include <cerrno>
#include <sstream>
#include <array>

HttpDashboard::HttpDashboard() {}

HttpDashboard::~HttpDashboard() {
    stop();
}

bool HttpDashboard::start(int port, int shm_key) {
    listen_fd_ = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd_ < 0) {
        GetLogger("HttpDashboard")->error("socket 创建失败: {}", strerror(errno));
        return false;
    }

    int opt = 1;
    setsockopt(listen_fd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port);

    if (bind(listen_fd_, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        GetLogger("HttpDashboard")->error("bind 端口 {} 失败: {}", port, strerror(errno));
        close(listen_fd_);
        listen_fd_ = -1;
        return false;
    }

    if (listen(listen_fd_, 5) < 0) {
        GetLogger("HttpDashboard")->error("listen 失败: {}", strerror(errno));
        close(listen_fd_);
        listen_fd_ = -1;
        return false;
    }

    running_ = true;
    thread_ = std::thread(&HttpDashboard::server_thread, this, port, shm_key);
    GetLogger("HttpDashboard")->info("HTTP 仪表盘已启动: http://0.0.0.0:{}/", port);
    return true;
}

void HttpDashboard::stop() {
    running_ = false;
    // 单独 close(listen_fd_) 不保证唤醒阻塞在 accept() 的 server_thread，
    // POSIX 不强制要求另一线程 close 同一 fd 能唤醒 accept。shutdown(SHUT_RDWR) 会让
    // 阻塞 accept 返回并 errno=EINVAL/EINVAL；多调用一次确保唤醒，再 close。
    if (listen_fd_ >= 0) {
        shutdown(listen_fd_, SHUT_RDWR);
        close(listen_fd_);
        listen_fd_ = -1;
    }
    if (thread_.joinable()) {
        thread_.join();
    }
}

void HttpDashboard::server_thread(int port, int shm_key) {
    while (running_) {
        struct sockaddr_in client_addr{};
        socklen_t addr_len = sizeof(client_addr);
        int client_fd = accept(listen_fd_, (struct sockaddr*)&client_addr, &addr_len);
        if (client_fd < 0) {
            if (errno == EINTR) continue;
            if (!running_) break;
            GetLogger("HttpDashboard")->warn("accept 失败: {}", strerror(errno));
            continue;
        }

        handle_client(client_fd, shm_key);
        close(client_fd);
    }
}

void HttpDashboard::handle_client(int client_fd, int shm_key) {
    struct timeval tv{5, 0};
    setsockopt(client_fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    std::array<char, 1024> buf{};
    ssize_t n = read(client_fd, buf.data(), buf.size() - 1);
    if (n <= 0) return;
    buf[n] = '\0';

    // 解析请求行
    std::string request(buf.data());
    std::string method, path;

    auto space1 = request.find(' ');
    if (space1 == std::string::npos) return;
    method = request.substr(0, space1);

    auto space2 = request.find(' ', space1 + 1);
    if (space2 == std::string::npos) return;
    path = request.substr(space1 + 1, space2 - space1 - 1);

    if (method != "GET") {
        std::string resp = http_response("405 Method Not Allowed", "text/plain", "Only GET allowed");
        write(client_fd, resp.data(), resp.size());
        return;
    }

    if (path == "/" || path == "/index.html") {
        std::string metrics = read_shm_metrics(shm_key);
        std::string html = R"(<!DOCTYPE html>
<html lang="zh-CN">
<head><meta charset="UTF-8"><meta name="viewport" content="width=device-width,initial-scale=1.0">
<title>AIoT 边缘网关监控</title>
<style>
  *{margin:0;padding:0;box-sizing:border-box;}
  body{font-family:-apple-system,'Segoe UI',sans-serif;background:#0d1117;color:#c9d1d9;padding:20px;}
  h1{color:#58a6ff;margin-bottom:24px;font-size:24px;}
  .grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(200px,1fr));gap:16px;margin-bottom:24px;}
  .card{background:#161b22;border:1px solid #30363d;border-radius:8px;padding:20px;}
  .card .label{font-size:12px;color:#8b949e;text-transform:uppercase;letter-spacing:0.5px;}
  .card .value{font-size:32px;font-weight:700;margin-top:8px;color:#f0f6fc;}
  .card .value.green{color:#3fb950;}
  .card .value.yellow{color:#d29922;}
  .card .value.red{color:#f85149;}
  .status-bar{display:flex;gap:24px;padding:12px 20px;background:#161b22;border:1px solid #30363d;border-radius:8px;margin-bottom:24px;font-size:14px;}
  .status-bar .item{display:flex;gap:8px;}
  .status-bar .dot{width:10px;height:10px;border-radius:50%;display:inline-block;margin-top:4px;}
  .dot.green{background:#3fb950;}
  .dot.red{background:#f85149;}
  .last-alarm{background:#161b22;border:1px solid #30363d;border-radius:8px;padding:16px 20px;font-size:14px;}
  .last-alarm .label{color:#8b949e;font-size:12px;}
  .last-alarm .msg{margin-top:6px;font-family:monospace;color:#f0f6fc;}
  .ts{text-align:center;color:#8b949e;font-size:12px;margin-top:20px;}
</style>
</head><body>
<h1>AIoT 边缘网关监控</h1>
<div class="status-bar" id="statusBar"><span id="statusText">读取中...</span></div>
<div class="grid" id="metricsGrid"></div>
<div class="last-alarm"><div class="label">最后一条告警</div><div class="msg" id="lastAlarm">-</div></div>
<div class="ts" id="timestamp"></div>
<script>
async function fetchMetrics(){
  try{
    const r=await fetch('/api/metrics');if(!r.ok)throw new Error('HTTP '+r.status);
    const d=await r.json();render(d);
  }catch(e){document.getElementById('statusText').innerHTML='<span class="dot red"></span> 连接失败: '+e.message;}
}
function render(d){
  const now=new Date().toLocaleString('zh-CN',{hour12:false});
  document.getElementById('timestamp').textContent='更新于: '+now;

  // 状态栏
  const aiOnline=d.ai_engine_online?'<span class="dot green"></span> AI引擎在线':'<span class="dot red"></span> AI引擎离线';
  const mqttOnline=d.mqtt_connected?'<span class="dot green"></span> MQTT已连接':'<span class="dot red"></span> MQTT断开';
  document.getElementById('statusBar').innerHTML=
    '<div class="item">'+aiOnline+'</div><div class="item">'+mqttOnline+'</div>'+
    '<div class="item">运行 '+d.uptime_sec+'s</div>';

  // 指标卡片
  const cards=[
    {label:'CPU 使用率',value:d.cpu_usage.toFixed(1)+'%',cls:d.cpu_usage>80?'red':d.cpu_usage>50?'yellow':'green'},
    {label:'内存使用率',value:d.mem_usage.toFixed(1)+'%',cls:d.mem_usage>80?'red':d.mem_usage>50?'yellow':'green'},
    {label:'NPU 温度',value:d.npu_temp_c.toFixed(1)+'°C',cls:d.npu_temp_c>85?'red':d.npu_temp_c>70?'yellow':'green'},
    {label:'推理帧率',value:d.inference_fps.toFixed(1)+' fps',cls:d.inference_fps>0?'green':'red'},
    {label:'在线节点',value:d.online_nodes,cls:'green'},
    {label:'消息总数',value:d.total_packets,cls:'green'},
    {label:'告警总数',value:d.total_alarms,cls:d.total_alarms>0?'yellow':'green'},
    {label:'活动告警',value:d.alarm_active,cls:d.alarm_active>0?'red':'green'},
  ];
  document.getElementById('metricsGrid').innerHTML=
    cards.map(c=>'<div class="card"><div class="label">'+c.label+'</div><div class="value '+c.cls+'">'+c.value+'</div></div>').join('');

  document.getElementById('lastAlarm').textContent=d.last_alarm||'-';
}
setInterval(fetchMetrics,2000);fetchMetrics();
</script>
</body></html>)";
        std::string resp = http_response("200 OK", "text/html; charset=utf-8", std::move(html));
        write(client_fd, resp.data(), resp.size());
    }
    else if (path == "/api/metrics") {
        std::string json = read_shm_metrics(shm_key);
        std::string resp = http_response("200 OK", "application/json", std::move(json));
        write(client_fd, resp.data(), resp.size());
    }
    else {
        std::string resp = http_response("404 Not Found", "text/plain", "Not Found");
        write(client_fd, resp.data(), resp.size());
    }
}

std::string HttpDashboard::read_shm_metrics(int shm_key) {
    ShmReader reader(shm_key);
    ShmBlock block;
    if (!reader.read(block)) {
        return R"({"error":"无法读取共享内存"})";
    }

    std::ostringstream json;
    json << "{";
    json << "\"uptime_sec\":" << block.uptime_sec << ",";
    json << "\"total_packets\":" << block.total_packets << ",";
    json << "\"total_alarms\":" << block.total_alarms << ",";
    json << "\"cpu_usage\":" << block.cpu_usage << ",";
    json << "\"mem_usage\":" << block.mem_usage << ",";
    json << "\"online_nodes\":" << block.online_nodes << ",";
    json << "\"mqtt_connected\":" << block.mqtt_connected << ",";
    json << "\"alarm_active\":" << block.alarm_active << ",";
    json << "\"npu_temp_c\":" << block.npu_temp_c << ",";
    json << "\"inference_fps\":" << block.inference_fps << ",";
    json << "\"ai_engine_online\":" << block.ai_engine_online << ",";
    json << "\"model_version\":" << block.model_version << ",";
    json << "\"last_detection_ts\":" << block.last_detection_ts << ",";
    json << "\"sensor_temp\":" << block.sensor_temp << ",";
    json << "\"sensor_hum\":" << block.sensor_hum << ",";
    json << "\"last_alarm\":\"" << json_escape(std::string(block.last_alarm, strnlen(block.last_alarm, sizeof(block.last_alarm)))) << "\",";
    json << "\"last_model_name\":\"" << json_escape(std::string(block.last_model_name, strnlen(block.last_model_name, sizeof(block.last_model_name)))) << "\"";
    json << "}";
    return json.str();
}

std::string HttpDashboard::http_response(const std::string& status,
                                          const std::string& content_type,
                                          const std::string& body) {
    std::ostringstream resp;
    resp << "HTTP/1.1 " << status << "\r\n"
         << "Content-Type: " << content_type << "\r\n"
         << "Content-Length: " << body.size() << "\r\n"
         << "Connection: close\r\n"
         << "Access-Control-Allow-Origin: *\r\n"
         << "\r\n"
         << body;
    return resp.str();
}

std::string HttpDashboard::json_escape(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (char c : s) {
        if (c == '"') out += "\\\"";
        else if (c == '\\') out += "\\\\";
        else if (c == '\n') out += "\\n";
        else if (c == '\r') out += "\\r";
        else if (c == '\t') out += "\\t";
        else out += c;
    }
    return out;
}
