#include <csignal>
#include <sys/wait.h>
#include <sys/stat.h>
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

// 就绪信号目录
static const char* kReadyDir = "/tmp/gateway_watchdog";

// 退避策略：记录最近 5 次重启时间
static constexpr int kMaxRestarts = 5;
static constexpr int kWindowSec = 30;
static constexpr int kReadyTimeoutSec = 10;
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

// 等待子进程就绪（检查标记文件，超时 10s）
bool wait_ready(int idx, pid_t pid) {
    char path[256];
    snprintf(path, sizeof(path), "%s/%s.ready", kReadyDir, g_names[idx]);

    // 先确保之前的 ready 文件已清理
    unlink(path);

    for (int i = 0; i < kReadyTimeoutSec * 10; ++i) {
        // 子进程退出了？不再等
        int status;
        pid_t result = waitpid(pid, &status, WNOHANG);
        if (result == pid) {
            printf("[watchdog] %s (PID %d) exited before ready\n", g_names[idx], pid);
            return false;
        }

        struct stat st;
        if (stat(path, &st) == 0) {
            printf("[watchdog] %s (PID %d) ready after %.1fs\n",
                   g_names[idx], pid, i * 0.1);
            return true;
        }
        usleep(100000);  // 100ms
    }

    printf("[watchdog] %s (PID %d) ready timeout (%ds), killing\n",
           g_names[idx], pid, kReadyTimeoutSec);
    kill(pid, SIGKILL);
    return false;
}

// fork + execv 启动子进程
pid_t start_child(int idx) {
    char* argv[] = {(char*)g_paths[idx], nullptr};

    pid_t pid = fork();
    if (pid < 0) {
        perror("fork failed");
        return -1;
    } else if (pid == 0) {
        // 子进程：继承父进程的工作目录（启动脚本已 cd 到项目根目录）
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
    // 创建就绪信号目录
    mkdir(kReadyDir, 0755);

    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    // 启动顺序：B → E → A → C
    g_children[0] = start_child(0);  // gateway_core
    if (g_children[0] == -1) return 1;
    if (!wait_ready(0, g_children[0])) {
        g_children[0] = 0;  // 等后续 waitpid 循环处理
    }

    g_children[3] = start_child(3);  // gateway_engine
    if (g_children[3] == -1) return 1;
    if (!wait_ready(3, g_children[3])) {
        g_children[3] = 0;
    }

    g_children[1] = start_child(1);  // gateway_access
    if (g_children[1] == -1) return 1;
    if (!wait_ready(1, g_children[1])) {
        g_children[1] = 0;
    }

    g_children[2] = start_child(2);  // gateway_monitor
    if (g_children[2] == -1) return 1;
    if (!wait_ready(2, g_children[2])) {
        g_children[2] = 0;
    }

    printf("[watchdog] All children ready, entering monitor loop\n");

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
                        if (g_children[i] > 0) {
                            wait_ready(i, g_children[i]);
                        }
                    }
                    break;
                }
            }
        }
        usleep(100000);
    }
}
