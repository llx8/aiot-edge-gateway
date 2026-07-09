#include "MonitorWindow.h"
#include "DashboardWidget.h"
#include "AlarmTableWidget.h"
#include "Logger.h"
#include <QSplitter>
#include <QStatusBar>
#include <sys/eventfd.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

// 构造函数，创建ShmReader对象，并传入DashboardWidget和AlarmTableWidget
MonitorWindow::MonitorWindow(QWidget* parent)
    : QMainWindow(parent)
{
    // 创建ShmReader对象， 使用与ShmPublisher相同的key
    reader_ = new ShmReader(0x47574D4D);

    // 创建DashboardWidget和AlarmTableWidget
    dashboard_ = new DashboardWidget(*reader_, this);
    alarm_table_ = new AlarmTableWidget(*reader_, this);

    // 使用QSplitter将两个Widget分割
    QSplitter* splitter = new QSplitter(Qt::Horizontal, this);
    splitter->addWidget(dashboard_);
    splitter->addWidget(alarm_table_);
    
    // 设置拉伸比例
    splitter->setStretchFactor(0, 1); // DashboardWidget占1份
    splitter->setStretchFactor(1, 2); // AlarmTableWidget占2份

    // 设为中心窗口
    setCentralWidget(splitter);

    // 窗口属性
    setWindowTitle("AIoT 边缘网关监控");
    resize(800, 600);

    // 1. 创建 eventfd
    notify_fd_ = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);

    // 2. 连接 B 的 monitor UDS，发送 eventfd
    if (notify_fd_ >= 0) {
        int sock = socket(AF_UNIX, SOCK_SEQPACKET, 0);
        if (sock >= 0) {
            struct sockaddr_un addr{};
            addr.sun_family = AF_UNIX;
            strncpy(addr.sun_path, "/tmp/gateway_monitor.sock", sizeof(addr.sun_path) - 1);
            if (::connect(sock, (struct sockaddr*)&addr, sizeof(addr)) == 0) {
                // 用 SCM_RIGHTS 发送 fd
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
                
                sendmsg(sock, &msg, 0);
            }
            ::close(sock);  // 传完就关
        }
    }

    // 3. 挂 QSocketNotifier
    notifier_ = new QSocketNotifier(notify_fd_, QSocketNotifier::Read, this);
    connect(notifier_, SIGNAL(activated(int)), this, SLOT(onShmNotify()));

    statusBar()->showMessage("监控运行中...");
}

// 析构函数，销毁ShmReader对象
MonitorWindow::~MonitorWindow() {
    delete reader_; // 释放ShmReader对象
    // qt对象树会自动释放dashboard_和alarm_table_
    GetLogger("gateway_monitor")->info("MonitorWindow destroyed");
    ::close(notify_fd_);
}

void MonitorWindow::onShmNotify() {
    uint64_t val;
    ::read(notify_fd_, &val, sizeof(val));   // 消费事件
    dashboard_->refresh();
    alarm_table_->refresh();
}