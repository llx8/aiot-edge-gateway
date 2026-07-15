#include "MonitorWindow.h"
#include "DashboardWidget.h"
#include "AlarmTableWidget.h"
#include "SensorChartWidget.h"
#include "AiSnapshotWidget.h"
#include "Logger.h"
#include "InternalMessage.h"
#include <QSplitter>
#include <QVBoxLayout>
#include <QStatusBar>
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
    ai_snapshot_->setMinimumHeight(280);
    alarm_table_ = new AlarmTableWidget(*reader_, this);

    // 左侧面板：Dashboard 在上，折线图在下
    QWidget* left_panel = new QWidget(this);
    QVBoxLayout* left_layout = new QVBoxLayout(left_panel);
    left_layout->setContentsMargins(0, 0, 0, 0);
    left_layout->addWidget(dashboard_);
    left_layout->addWidget(chart_);
    left_layout->addWidget(ai_snapshot_);
    left_layout->setStretch(0, 1);
    left_layout->setStretch(1, 1);
    left_layout->setStretch(2, 1);

    QSplitter* splitter = new QSplitter(Qt::Horizontal, this);
    splitter->addWidget(left_panel);
    splitter->addWidget(alarm_table_);
    splitter->setStretchFactor(0, 1);
    splitter->setStretchFactor(1, 2);

    setCentralWidget(splitter);
    setWindowTitle("AIoT 边缘网关监控");
    resize(800, 600);

    // 1. 创建 eventfd
    notify_fd_ = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);

    // 2. 连接 B 的 monitor UDS，发送 eventfd + 保持连接接收 JPEG
    uds_buf_.resize(kUdsBufSize);
    if (notify_fd_ >= 0) {
        uds_fd_ = socket(AF_UNIX, SOCK_SEQPACKET, 0);
        if (uds_fd_ >= 0) {
            struct sockaddr_un addr{};
            addr.sun_family = AF_UNIX;
            strncpy(addr.sun_path, "/tmp/gateway_monitor.sock", sizeof(addr.sun_path) - 1);
            if (::connect(uds_fd_, (struct sockaddr*)&addr, sizeof(addr)) == 0) {
                // 用 SCM_RIGHTS 发送 eventfd
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

                // 保持连接，注册 QSocketNotifier 接收 JPEG 数据
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

    statusBar()->showMessage("监控运行中...");
}

MonitorWindow::~MonitorWindow() {
    ::close(notify_fd_);
    if (uds_fd_ >= 0) ::close(uds_fd_);
}

void MonitorWindow::onShmNotify() {
    uint64_t val;
    ::read(notify_fd_, &val, sizeof(val));
    dashboard_->refresh();
    alarm_table_->refresh();
    ShmBlock block;
    if (reader_->read(block)) {
        chart_->update_value(static_cast<float>(block.total_packets));
    }
}

void MonitorWindow::onUdsData() {
    // 从 UDS 读取数据（JPEG 快照等）
    ssize_t n = ::read(uds_fd_, uds_buf_.data(), uds_buf_.size());
    if (n <= 0) {
        if (n == 0 || errno != EAGAIN) {
            GetLogger("MonitorWindow")->warn("UDS connection lost");
            ::close(uds_fd_);
            uds_fd_ = -1;
        }
        return;
    }

    // 解码 InternalMessage
    auto result = decode_internal_msg(uds_buf_.data(), static_cast<size_t>(n));
    if (!result.ok) return;

    // JPEG 快照 (tlv_type=0x07)
    if (result.msg.tlv_type == 0x07 && !result.msg.payload.empty()) {
        // payload 格式: [4B: num_detections] + [num * 24B: Detection] + [JPEG data]
        std::vector<QRect> boxes;
        std::vector<QString> labels;

        const auto& payload = result.msg.payload;
        if (payload.size() >= 4) {
            int32_t num_dets = 0;
            std::memcpy(&num_dets, payload.data(), 4);
            constexpr size_t DET_SIZE = 24;  // sizeof(Detection) = 4 floats + 1 float + 1 int
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
            // 提取 JPEG 部分
            size_t jpeg_off = 4 + num_dets * DET_SIZE;
            if (jpeg_off < payload.size()) {
                std::vector<uint8_t> jpeg_data(payload.begin() + jpeg_off, payload.end());
                ai_snapshot_->update_snapshot(jpeg_data, boxes, labels);
                GetLogger("MonitorWindow")->info("Received JPEG snapshot: {}KB, {} detections",
                    jpeg_data.size() / 1024, num_dets);
            }
        }
    }
}