#!/bin/bash
# cgroups v2 资源隔离脚本
# 限制 gateway_engine（进程 E）CPU ≤ 60%、内存 ≤ 2GB
# 用法: sudo bash scripts/setup_cgroups.sh [on|off]
#
# RK3588 内核 6.1.x 默认使用 cgroups v2（统一层级），
# 非旧版 v1 的 cpu/memory 分开挂载。

set -e

CGROUP_BASE="/sys/fs/cgroup"
CGROUP_PATH="$CGROUP_BASE/gateway_engine"
ENGINE_PID_FILE="/tmp/gateway_engine.pid"

ensure_cgroup() {
    if [ ! -d "$CGROUP_PATH" ]; then
        mkdir -p "$CGROUP_PATH"
    fi
    # 确保 subtree_control 允许子 cgroup 控制器
    if [ -f "$CGROUP_BASE/cgroup.subtree_control" ]; then
        echo "+cpu +memory" > "$CGROUP_BASE/cgroup.subtree_control" 2>/dev/null || true
    fi
}

apply_limits() {
    ensure_cgroup

    # CPU 限制：60% = 60000/100000
    # cgroups v2 cpu.max 格式: $MAX $PERIOD
    echo "60000 100000" > "$CGROUP_PATH/cpu.max" 2>/dev/null || true
    echo "CPU:  已限制为 60% (60000/100000)"

    # 内存限制：2GB
    echo "2147483648" > "$CGROUP_PATH/memory.max" 2>/dev/null || true
    echo "内存: 已限制为 2GB (2147483648 bytes)"

    # 将进程 E 加入 cgroup（通过 cgroup.procs）
    if [ -f "$ENGINE_PID_FILE" ]; then
        PID=$(cat "$ENGINE_PID_FILE")
        if kill -0 "$PID" 2>/dev/null; then
            echo "$PID" > "$CGROUP_PATH/cgroup.procs" 2>/dev/null || true
            echo "进程 E (PID $PID) 已加入 cgroup 限制"
        else
            echo "警告: 进程 E (PID $PID) 未运行，cgroup 已创建但未附加进程"
        fi
    else
        echo "警告: $ENGINE_PID_FILE 不存在，cgroup 已创建但未附加进程"
        echo "进程 E 启动后，手动执行: echo <PID> > $CGROUP_PATH/cgroup.procs"
    fi
}

remove_limits() {
    if [ -d "$CGROUP_PATH" ]; then
        if [ -f "$CGROUP_PATH/cgroup.procs" ]; then
            while read -r pid; do
                echo "$pid" > "$CGROUP_BASE/cgroup.procs" 2>/dev/null || true
            done < "$CGROUP_PATH/cgroup.procs"
        fi
        rmdir "$CGROUP_PATH" 2>/dev/null || true
        echo "cgroup gateway_engine 已移除"
    fi
}

case "${1:-on}" in
    on|start|apply)
        apply_limits
        ;;
    off|stop|remove)
        remove_limits
        ;;
    *)
        echo "用法: $0 [on|off]"
        exit 1
        ;;
esac
