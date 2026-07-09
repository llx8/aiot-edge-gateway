#include "MonitorWindow.h"
#include <QApplication>
#include "Logger.h"
#include <fstream>
#include <sys/stat.h>

int main(int argc, char* argv[]) {
    // 初始化日志
    auto logger = GetLogger("gateway_monitor");
    logger->info("Starting gateway_monitor...");

    // 创建QApplication对象
    QApplication app(argc, argv);

    // 创建并显示MonitorWindow
    MonitorWindow window;
    window.show();

    // 通知 Watchdog：已就绪
    mkdir("/tmp/gateway_watchdog", 0755);
    std::ofstream("/tmp/gateway_watchdog/gateway_monitor.ready") << "1";

    // 运行Qt事件循环
    int ret = app.exec();

    return ret;
}