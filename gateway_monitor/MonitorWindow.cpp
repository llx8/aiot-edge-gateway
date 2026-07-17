#include "MonitorWindow.h"
#include "DashboardWidget.h"
#include "AlarmTableWidget.h"
#include "SensorChartWidget.h"
#include "AiSnapshotWidget.h"
#include "Logger.h"
#include "InternalMessage.h"
#include <QSplitter>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QStatusBar>
#include <QFrame>
#include <QLabel>
#include <QApplication>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QDateTime>
#include <sys/eventfd.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <cstring>

MonitorWindow::MonitorWindow(QWidget* parent)
    : QMainWindow(parent)
{
    reader_ = std::make_unique<ShmReader>(0x47574D4D);

    dashboard_ = new DashboardWidget(*reader_, this);
    chart_ = new SensorChartWidget(this);
    ai_snapshot_ = new AiSnapshotWidget(this);
    alarm_table_ = new AlarmTableWidget(this);

    // ── 左侧上下三段：卡片 → 折线图 → 告警列表 ──
    auto* left_panel = new QWidget(this);
    auto* left_layout = new QVBoxLayout(left_panel);
    left_layout->setContentsMargins(4, 4, 2, 4);
    left_layout->setSpacing(4);

    // 3×2 卡片
    auto* card_section = new QFrame(left_panel);
    card_section->setStyleSheet("QFrame{background:#0d1117; border:none;}");
    auto* card_inner = new QHBoxLayout(card_section);
    card_inner->setContentsMargins(0, 0, 0, 0);
    card_inner->addWidget(dashboard_);

    // 折线图
    auto* chart_frame = new QFrame(left_panel);
    chart_frame->setStyleSheet(
        "QFrame{background:#161b22; border:1px solid #30363d; border-radius:6px;}");
    auto* chart_lay = new QVBoxLayout(chart_frame);
    chart_lay->setContentsMargins(4, 4, 4, 4);
    auto* chart_title = new QLabel("Sensor Temp & Humidity", chart_frame);
    chart_title->setStyleSheet("font-size:10px; color:#8b949e; border:none;");
    chart_lay->addWidget(chart_title);
    chart_lay->addWidget(chart_);

    // 告警列表
    auto* alarm_frame = new QFrame(left_panel);
    alarm_frame->setStyleSheet(
        "QFrame{background:#161b22; border:1px solid #30363d; border-radius:6px;}");
    auto* alarm_lay = new QVBoxLayout(alarm_frame);
    alarm_lay->setContentsMargins(4, 4, 4, 4);
    auto* alarm_title = new QLabel("Alarm Log", alarm_frame);
    alarm_title->setStyleSheet("font-size:10px; color:#8b949e; border:none;");
    alarm_lay->addWidget(alarm_title);
    alarm_lay->addWidget(alarm_table_);

    left_layout->addWidget(card_section, 0);
    left_layout->addWidget(chart_frame, 1);
    left_layout->addWidget(alarm_frame, 1);

    // ── 右侧：AI 检测画面 + 底部状态 ──
    auto* right_panel = new QWidget(this);
    auto* right_layout = new QVBoxLayout(right_panel);
    right_layout->setContentsMargins(2, 4, 4, 4);
    right_layout->setSpacing(4);

    auto* snapshot_frame = new QFrame(right_panel);
    snapshot_frame->setStyleSheet(
        "QFrame{background:#161b22; border:1px solid #30363d; border-radius:6px;}");
    auto* snap_lay = new QVBoxLayout(snapshot_frame);
    snap_lay->setContentsMargins(4, 4, 4, 4);
    auto* snap_title = new QLabel("AI Detection", snapshot_frame);
    snap_title->setStyleSheet("font-size:10px; color:#8b949e; border:none;");
    snap_lay->addWidget(snap_title);
    snap_lay->addWidget(ai_snapshot_);

    auto* status_frame = new QFrame(right_panel);
    status_frame->setStyleSheet(
        "QFrame{background:#161b22; border:1px solid #30363d; border-radius:4px;}");
    status_frame->setFixedHeight(24);
    auto* status_lay = new QHBoxLayout(status_frame);
    status_lay->setContentsMargins(8, 0, 8, 0);
    auto* status_text = new QLabel("Monitor Running...", status_frame);
    status_text->setStyleSheet("font-size:10px; color:#8b949e; border:none;");
    status_lay->addWidget(status_text);

    right_layout->addWidget(snapshot_frame, 1);
    right_layout->addWidget(status_frame, 0);

    auto* splitter = new QSplitter(Qt::Horizontal, this);
    splitter->addWidget(left_panel);
    splitter->addWidget(right_panel);
    splitter->setStretchFactor(0, 1);
    splitter->setStretchFactor(1, 1);

    setCentralWidget(splitter);
    setWindowTitle("AIoT Edge Gateway Monitor");
    resize(1280, 720);
    setStyleSheet("QMainWindow{background:#0d1117;}");

    // 1. 创建 eventfd
    notify_fd_ = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);

    // 2. 连接 B 的 monitor UDS
    uds_buf_.resize(kUdsBufSize);
    if (notify_fd_ >= 0) {
        uds_fd_ = socket(AF_UNIX, SOCK_SEQPACKET, 0);
        if (uds_fd_ >= 0) {
            struct sockaddr_un addr{};
            addr.sun_family = AF_UNIX;
            strncpy(addr.sun_path, "/tmp/gateway_monitor.sock", sizeof(addr.sun_path) - 1);
            if (::connect(uds_fd_, (struct sockaddr*)&addr, sizeof(addr)) == 0) {
                char dummy = 0;
                struct iovec iov = { &dummy, 1 };
                char cmsg_buf[CMSG_SPACE(sizeof(int))];
                struct msghdr msg = {};
                msg.msg_iov = &iov;
                msg.msg_iovlen = 1;
                msg.msg_control = cmsg_buf;
                msg.msg_controllen = sizeof(cmsg_buf);

                struct cmsghdr* cmsg = CMSG_FIRSTHDR(&msg);
                cmsg->cmsg_level = SOL_SOCKET;
                cmsg->cmsg_type  = SCM_RIGHTS;
                cmsg->cmsg_len   = CMSG_LEN(sizeof(int));
                *(int*)CMSG_DATA(cmsg) = notify_fd_;
                msg.msg_controllen = CMSG_SPACE(sizeof(int));

                sendmsg(uds_fd_, &msg, 0);

                uds_notifier_ = new QSocketNotifier(uds_fd_, QSocketNotifier::Read, this);
                connect(uds_notifier_, SIGNAL(activated(int)), this, SLOT(onUdsData()));
            } else {
                ::close(uds_fd_);
                uds_fd_ = -1;
            }
        }
    }

    // 3. 挂 QSocketNotifier
    notifier_ = new QSocketNotifier(notify_fd_, QSocketNotifier::Read, this);
    connect(notifier_, SIGNAL(activated(int)), this, SLOT(onShmNotify()));

    // 4. QTimer 定时刷新仪表盘
    refresh_timer_ = new QTimer(this);
    connect(refresh_timer_, SIGNAL(timeout()), this, SLOT(onTimerRefresh()));
    refresh_timer_->start(1000);

    // 5. QTimer 定时查询告警历史（每 2 秒）
    alarm_query_timer_ = new QTimer(this);
    connect(alarm_query_timer_, SIGNAL(timeout()), this, SLOT(onAlarmQuery()));
    alarm_query_timer_->start(2000);
}

MonitorWindow::~MonitorWindow() {
    ::close(notify_fd_);
    if (uds_fd_ >= 0) ::close(uds_fd_);
}

void MonitorWindow::onShmNotify() {
    uint64_t val;
    ::read(notify_fd_, &val, sizeof(val));
}

void MonitorWindow::onTimerRefresh() {
    dashboard_->refresh();
    ShmBlock block;
    if (reader_->read(block)) {
        chart_->update_value(block.sensor_temp, block.sensor_hum);
    }
}

void MonitorWindow::sendAlarmQuery() {
    if (uds_fd_ < 0) return;
    InternalMessage query;
    query.source_type = 0;
    query.node_id = 0;
    query.tlv_type = TLV_ALARM_QUERY;
    std::string id_str = std::to_string(last_alarm_id_);
    query.payload.assign(id_str.begin(), id_str.end());
    auto encoded = encode_internal_msg(query);
    ::write(uds_fd_, encoded.data(), encoded.size());
}

void MonitorWindow::onAlarmQuery() {
    sendAlarmQuery();
}

void MonitorWindow::handleAlarmResponse(const std::vector<uint8_t>& payload) {
    QByteArray json_data(reinterpret_cast<const char*>(payload.data()),
                         static_cast<int>(payload.size()));
    QJsonParseError err;
    QJsonDocument doc = QJsonDocument::fromJson(json_data, &err);
    if (err.error != QJsonParseError::NoError) return;
    if (!doc.isArray()) return;
    QJsonArray arr = doc.array();
    if (arr.isEmpty()) return;  // 无新告警，不刷新

    std::vector<AlarmEntry> new_alarms;
    new_alarms.reserve(static_cast<size_t>(arr.size()));
    for (const auto& val : arr) {
        if (!val.isObject()) continue;
        QJsonObject obj = val.toObject();
        AlarmEntry e;
        e.id = obj["id"].toInt();
        e.severity = 2;
        e.detail = obj["detail"].toString();

        QJsonDocument detail_doc = QJsonDocument::fromJson(e.detail.toUtf8(), &err);
        if (err.error == QJsonParseError::NoError && detail_doc.isObject()) {
            QJsonObject detail_obj = detail_doc.object();
            if (detail_obj.contains("severity"))
                e.severity = detail_obj["severity"].toInt();
            if (detail_obj.contains("context")) {
                QJsonObject ctx = detail_obj["context"].toObject();
                QString ctx_str;
                if (ctx.contains("temperature"))
                    ctx_str += QString("T:%1°C ").arg(ctx["temperature"].toDouble(), 0, 'f', 1);
                if (ctx.contains("humidity"))
                    ctx_str += QString("H:%1%% ").arg(ctx["humidity"].toDouble(), 0, 'f', 1);
                e.detail = ctx_str + e.detail;
            }
        }
        e.time = QDateTime::currentDateTime().toString("HH:mm:ss");
        new_alarms.push_back(e);

        if (e.id > last_alarm_id_) last_alarm_id_ = e.id;
    }

    alarm_table_->appendAlarms(new_alarms);
}

void MonitorWindow::onUdsData() {
    ssize_t n = ::read(uds_fd_, uds_buf_.data(), uds_buf_.size());
    if (n <= 0) {
        if (n == 0 || errno != EAGAIN) {
            GetLogger("MonitorWindow")->warn("UDS connection lost");
            ::close(uds_fd_);
            uds_fd_ = -1;
        }
        return;
    }

    auto result = decode_internal_msg(uds_buf_.data(), static_cast<size_t>(n));
    if (!result.ok) return;

    // 告警查询响应
    if (result.msg.tlv_type == TLV_ALARM_QUERY_RESPONSE) {
        handleAlarmResponse(result.msg.payload);
        return;
    }

    // JPEG 快照
    if (result.msg.tlv_type == 0x07 && !result.msg.payload.empty()) {
        std::vector<QRect> boxes;
        std::vector<QString> labels;

        const auto& payload = result.msg.payload;
        if (payload.size() >= 4) {
            int32_t num_dets = 0;
            std::memcpy(&num_dets, payload.data(), 4);
            constexpr size_t DET_SIZE = 24;
            if (num_dets > 0 && payload.size() >= 4 + num_dets * DET_SIZE) {
                for (int32_t i = 0; i < num_dets; ++i) {
                    size_t off = 4 + i * DET_SIZE;
                    float x, y, w, h, conf;
                    int32_t class_id;
                    std::memcpy(&x, payload.data() + off, 4);
                    std::memcpy(&y, payload.data() + off + 4, 4);
                    std::memcpy(&w, payload.data() + off + 8, 4);
                    std::memcpy(&h, payload.data() + off + 12, 4);
                    std::memcpy(&conf, payload.data() + off + 16, 4);
                    std::memcpy(&class_id, payload.data() + off + 20, 4);
                    boxes.push_back(QRect(static_cast<int>(x), static_cast<int>(y),
                                          static_cast<int>(w), static_cast<int>(h)));
                    labels.push_back(QString("cls%1 %2%").arg(class_id).arg(conf * 100, 0, 'f', 0));
                }
            }
            size_t jpeg_off = 4 + num_dets * DET_SIZE;
            if (jpeg_off < payload.size()) {
                std::vector<uint8_t> jpeg_data(payload.begin() + jpeg_off, payload.end());
                ai_snapshot_->update_snapshot(jpeg_data, boxes, labels);
            }
        }
    }
}
