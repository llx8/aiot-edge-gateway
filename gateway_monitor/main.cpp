#include "MonitorWindow.h"
#include <QApplication>
#include "Logger.h"
#include <fstream>
#include <sys/stat.h>
#include <cstdlib>
#include <csignal>

int main(int argc, char* argv[]) {
    if (!qEnvironmentVariableIsSet("DISPLAY") && !qEnvironmentVariableIsSet("WAYLAND_DISPLAY")) {
        qputenv("QT_QPA_PLATFORM", "offscreen");
    }

    auto logger = GetLogger("gateway_monitor");
    logger->info("Starting gateway_monitor...");

    signal(SIGPIPE, SIG_IGN);  // UDS write 失败不能杀进程

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