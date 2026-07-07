#include <csignal>
#include <sys/wait.h>
#include <unistd.h>
#include <cstdio>
#include <cstring>
#include <ctime>

// 全局变量，用于保存子进程PID
// 索引：0=B(gateway_core), 1=A(gateway_access), 2=C(gateway_monitor), 3=E(gateway_engine)
pid_t g_children[4] = {0, 0, 0, 0};

const char* g_names[4] = {"gateway_core", "gateway_access", "gateway_monitor", "gateway_engine"};
const char* g_paths[4] = {
    "./build/gateway_core/gateway_core",
    "./build/gateway_access/gateway_access",
    "./build/gateway_monitor/gateway_monitor",
    "./build/gateway_engine/gateway_engine"
};

// 退避策略：记录最近 5 次重启时间
static constexpr int kMaxRestarts = 5;
static constexpr int kWindowSec = 30;
time_t g_restart_times[4][kMaxRestarts] = {};
int g_restart_idx[4] = {0, 0, 0, 0};

// 信号处理函数
void signal_handler(int signum) {
    for (int i = 0; i < 4; ++i) {
        if (g_children[i] > 0) {
            kill(g_children[i], SIGTERM);
        }
    }
}

// 检查是否应该重启（退避策略）
bool should_restart(int idx) {
    time_t now = time(nullptr);
    int slot = g_restart_idx[idx] % kMaxRestarts;
    g_restart_times[idx][slot] = now;
    g_restart_idx[idx]++;

    int count = 0;
    for (int i = 0; i < kMaxRestarts; i++) {
        if (now - g_restart_times[idx][i] <= kWindowSec) {
            count++;
        }
    }
    if (count >= kMaxRestarts) {
        printf("[FATAL] %s restarted %d times in %ds, pausing restart\n",
               g_names[idx], kMaxRestarts, kWindowSec);
        return false;
    }
    return true;
}

// fork + execv 启动子进程
pid_t start_child(int idx) {
    char* argv[] = {(char*)g_paths[idx], nullptr};

    pid_t pid = fork();
    if (pid < 0) {
        perror("fork failed");
        return -1;
    } else if (pid == 0) {
        // 子进程：切换到项目根目录再 execv
        chdir("/home/lxxxxl/桌面/GitHub/aiot-edge-gateway");
        execv(g_paths[idx], argv);
        perror("execv failed");
        _exit(1);
    } else {
        time_t now = time(nullptr);
        char buf[64];
        strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", localtime(&now));
        printf("[%s] Started %s (PID %d)\n", buf, g_names[idx], pid);
        return pid;
    }
}

int main() {
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    // 启动顺序：B → E → A → C（间隔 500ms）
    g_children[0] = start_child(0);  // gateway_core
    if (g_children[0] == -1) return 1;
    usleep(500000);

    g_children[3] = start_child(3);  // gateway_engine
    if (g_children[3] == -1) return 1;
    usleep(500000);

    g_children[1] = start_child(1);  // gateway_access
    if (g_children[1] == -1) return 1;
    usleep(500000);

    g_children[2] = start_child(2);  // gateway_monitor
    if (g_children[2] == -1) return 1;

    // waitpid 循环
    while (true) {
        int status;
        pid_t pid = waitpid(-1, &status, WNOHANG);

        if (pid > 0) {
            for (int i = 0; i < 4; ++i) {
                if (g_children[i] == pid) {
                    if (WIFEXITED(status)) {
                        printf("[watchdog] %s (PID %d) exited with status %d\n",
                               g_names[i], pid, WEXITSTATUS(status));
                    } else if (WIFSIGNALED(status)) {
                        printf("[watchdog] %s (PID %d) killed by signal %d\n",
                               g_names[i], pid, WTERMSIG(status));
                    }

                    if (should_restart(i)) {
                        printf("[watchdog] Restarting %s in 1s...\n", g_names[i]);
                        sleep(1);
                        g_children[i] = start_child(i);
                    }
                    break;
                }
            }
        }
        usleep(100000);
    }
}
