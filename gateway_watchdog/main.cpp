#include <atomic>
#include <csignal>
#include <sys/wait.h>
#include <sys/stat.h>
#include <unistd.h>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <algorithm>

// 全局变量，用于保存子进程PID
// 索引：0=B(gateway_core), 1=A(gateway_access), 2=C(gateway_monitor), 3=E(gateway_engine)
pid_t g_children[4] = {0, 0, 0, 0};
// 子进程存活标记：start_child 设 true，waitpid reap 后设 false。
// 替代 kill(pid,0) 检测——已 reap 的 PID 可能被 OS 复用造成假"存活"。
bool g_child_alive[4] = {false, false, false, false};

// 优雅退出标志：原实现主循环为 while(true)，收到 SIGTERM 后只杀子进程不退出，
// 子进程退出又被主循环重启，导致 watchdog 无法正常关闭（必须 kill -9）
static std::atomic<sig_atomic_t> g_running{1};

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
static constexpr int kCooldownSec = 60;  // should_restart 触底后冷却 60s 再重试
time_t g_restart_times[4][kMaxRestarts] = {};
int g_restart_idx[4] = {0, 0, 0, 0};
time_t g_retry_at[4] = {0, 0, 0, 0};  // 0 = 无待重试；>0 = 到点应该重启

// 信号处理函数
void signal_handler(int signum) {
    (void)signum;
    g_running.store(0);
    for (int i = 0; i < 4; ++i) {
        if (g_children[i] > 0) {
            kill(g_children[i], SIGTERM);
        }
    }
}

// 检查是否应该重启；返回窗内最近失败次数
int restart_count_in_window(int idx) {
    time_t now = time(nullptr);
    int count = 0;
    for (int i = 0; i < kMaxRestarts; ++i) {
        if (g_restart_times[idx][i] > 0 && now - g_restart_times[idx][i] <= kWindowSec) {
            ++count;
        }
    }
    return count;
}

// 记录一次重启尝试，并判定是否允许立即重启（不是窗内第 5 次）
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
        printf("[FATAL] %s restarted %d times in %ds, cooldown %ds\n",
               g_names[idx], kMaxRestarts, kWindowSec, kCooldownSec);
        return false;
    }
    return true;
}

// 清空某槽位的退避历史（冷却结束后重给窗口）
void reset_restart_history(int idx) {
    g_restart_idx[idx] = 0;
    for (int i = 0; i < kMaxRestarts; ++i) g_restart_times[idx][i] = 0;
}

// 等待子进程就绪（检查标记文件，超时 10s）
bool wait_ready(int idx, pid_t pid) {
    char path[256];
    snprintf(path, sizeof(path), "%s/%s.ready", kReadyDir, g_names[idx]);

    // 先确保之前的 ready 文件已清理
    unlink(path);

    for (int i = 0; i < kReadyTimeoutSec * 10; ++i) {
        // 用 kill(pid,0) 检测进程是否还在，绝不在此 reap。
        // 原先用 waitpid(WNOHANG) 会抢先 reap 启动失败退出的子进程，返回后 g_children 被清零，
        // 主循环 waitpid(-1) 再也收不到该 PID -> 子进程永远不会被重启。
        // 改为只探测存活，reap + 重启交给主循环。
        if (kill(pid, 0) != 0) {
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
        g_child_alive[idx] = true;
        return pid;
    }
}

// 杀掉所有已启动的子进程并 reap，避免启动中途失败留孤儿
// （否则 systemd 3s 后拉起新 watchdog，会和本实例抢 UDS/MQTT/RKNN）
void kill_and_reap_all_children() {
    for (int i = 0; i < 4; ++i) {
        if (g_children[i] > 0) {
            kill(g_children[i], SIGTERM);
        }
    }
    for (int i = 0; i < 4; ++i) {
        if (g_children[i] > 0) {
            int status;
            for (int t = 0; t < 50; ++t) {
                if (waitpid(g_children[i], &status, WNOHANG) != 0) break;
                usleep(100000);
            }
            if (waitpid(g_children[i], &status, WNOHANG) == 0) {
                kill(g_children[i], SIGKILL);
                waitpid(g_children[i], &status, 0);
            }
            g_child_alive[i] = false;
        }
    }
}

int main() {
    // 创建就绪信号目录
    mkdir(kReadyDir, 0755);

    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    signal(SIGPIPE, SIG_IGN);  // 守护进程不应被 SIGPIPE 杀死

    // 启动顺序：B → E → A → C
    g_children[0] = start_child(0);  // gateway_core
    if (g_children[0] == -1) { kill_and_reap_all_children(); return 1; }
    // wait_ready 失败时不清零 g_children：保留 PID 让主循环 reap+重启（清零则永不重启）
    wait_ready(0, g_children[0]);

    g_children[3] = start_child(3);  // gateway_engine
    if (g_children[3] == -1) { kill_and_reap_all_children(); return 1; }
    wait_ready(3, g_children[3]);

    g_children[1] = start_child(1);  // gateway_access
    if (g_children[1] == -1) { kill_and_reap_all_children(); return 1; }
    wait_ready(1, g_children[1]);

    g_children[2] = start_child(2);  // gateway_monitor
    if (g_children[2] == -1) { kill_and_reap_all_children(); return 1; }
    wait_ready(2, g_children[2]);

    printf("[watchdog] All children ready, entering monitor loop\n");

    // waitpid 循环
    while (g_running.load()) {
        // 先看是否有冷却到期的槽位需要重新拉起（should_restart 触底后的恢复路径，
        // 否则该槽位永久失活——主循环 waitpid(-1) 永不再为已 reap PID 返回）
        time_t now = time(nullptr);
        for (int i = 0; i < 4; ++i) {
            if (g_retry_at[i] > 0 && now >= g_retry_at[i] && g_children[i] == 0) {
                printf("[watchdog] cooldown expired, retrying %s\n", g_names[i]);
                reset_restart_history(i);
                g_retry_at[i] = 0;
                g_children[i] = start_child(i);
                if (g_children[i] > 0) {
                    wait_ready(i, g_children[i]);
                } else {
                    // fork 又失败：再挂冷却。避免立刻又调用
                    g_retry_at[i] = now + kCooldownSec;
                }
            }
        }

        int status;
        pid_t pid = waitpid(-1, &status, WNOHANG);

        if (pid > 0) {
            for (int i = 0; i < 4; ++i) {
                if (g_children[i] == pid) {
                    g_child_alive[i] = false;
                    if (WIFEXITED(status)) {
                        printf("[watchdog] %s (PID %d) exited with status %d\n",
                               g_names[i], pid, WEXITSTATUS(status));
                    } else if (WIFSIGNALED(status)) {
                        printf("[watchdog] %s (PID %d) killed by signal %d\n",
                               g_names[i], pid, WTERMSIG(status));
                    }

                    if (should_restart(i)) {
                        // 依赖检查：A/E/C 依赖 B (gateway_core) 的 UDS 服务
                        // 若 B 已死，先重启 B 再重启当前进程
                        // 用 g_child_alive[0] 替代 kill(g_children[0],0)，
                        // 避免已 reap PID 被 OS 复用造成假"存活"
                        if (i != 0 && !g_child_alive[0] && g_children[0] > 0) {
                            printf("[watchdog] %s depends on %s (dead), restarting dependency first\n",
                                   g_names[i], g_names[0]);
                            g_children[0] = start_child(0);
                            if (g_children[0] > 0) wait_ready(0, g_children[0]);
                        }
                        // 指数退避：1→2→4→8（最多 8s），上限 16
                        int backoff = 1 << std::min(restart_count_in_window(i) - 1, 4);
                        if (backoff < 1) backoff = 1;
                        if (backoff > 16) backoff = 16;
                        printf("[watchdog] Restarting %s in %ds...\n", g_names[i], backoff);
                        sleep(backoff);
                        g_children[i] = start_child(i);
                        if (g_children[i] > 0) {
                            wait_ready(i, g_children[i]);
                        }
                    } else {
                        // should_restart 触底：挂冷却，到点再重试（不能彻底放弃）
                        g_retry_at[i] = time(nullptr) + kCooldownSec;
                        g_children[i] = 0;
                        printf("[watchdog] %s scheduled retry after %ds\n",
                               g_names[i], kCooldownSec);
                    }
                    break;
                }
            }
        }
        usleep(100000);
    }

    // 优雅退出：signal_handler 已向子进程发 SIGTERM，这里兜底再发一次并 reap，
    // 超时未退出的强杀，避免僵尸进程。原实现无此清理且主循环不退出。
    kill_and_reap_all_children();
    printf("[watchdog] shutdown complete\n");
    return 0;
}
