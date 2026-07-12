#!/bin/bash
# cgroups 资源隔离脚本
# 限制 gateway_engine（进程 E）CPU ≤ 60%、内存 ≤ 2GB
# 用法: sudo bash scripts/setup_cgroups.sh [on|off]
#
# cgroups v1 路径参考:
#   CPU:  /sys/fs/cgroup/cpu/gateway_engine/
#   内存: /sys/fs/cgroup/memory/gateway_engine/

set -e

CGROUP_CPU="/sys/fs/cgroup/cpu/gateway_engine"
CGROUP_MEM="/sys/fs/cgroup/memory/gateway_engine"
ENGINE_PID_FILE="/tmp/gateway_engine.pid"

ensure_cgroup_dirs() {
    if [ ! -d "$CGROUP_CPU" ]; then
        mkdir -p "$CGROUP_CPU"
    fi
    if [ ! -d "$CGROUP_MEM" ]; then
        mkdir -p "$CGROUP_MEM"
    fi
}

apply_limits() {
    ensure_cgroup_dirs

    # CPU 限制：60% = 60000/100000
    # cfs_period_us = 100000 (默认周期 100ms)
    # cfs_quota_us = 60000 (每周期最多 60ms)
    echo 100000 > "$CGROUP_CPU/cpu.cfs_period_us" 2>/dev/null || true
    echo 60000  > "$CGROUP_CPU/cpu.cfs_quota_us"  2>/dev/null || true
    echo "CPU:  已限制为 60% (100000/60000)"

    # 内存限制：2GB
    echo 2147483648 > "$CGROUP_MEM/memory.limit_in_bytes" 2>/dev/null || true
    echo "内存: 已限制为 2GB"

    # 将进程 E 加入 cgroup
    if [ -f "$ENGINE_PID_FILE" ]; then
        PID=$(cat "$ENGINE_PID_FILE")
        if kill -0 "$PID" 2>/dev/null; then
            echo "$PID" > "$CGROUP_CPU/tasks" 2>/dev/null || true
            echo "$PID" > "$CGROUP_MEM/tasks" 2>/dev/null || true
            echo "进程 E (PID $PID) 已加入 cgroup 限制"
        else
            echo "警告: 进程 E (PID $PID) 未运行，cgroup 已创建但未附加进程"
        fi
    else
        echo "警告: $ENGINE_PID_FILE 不存在，cgroup 已创建但未附加进程"
        echo "进程 E 启动后，手动执行: echo <PID> > $CGROUP_CPU/tasks"
    fi
}

remove_limits() {
    # 移除限制（把任务移回根 cgroup）
    if [ -d "$CGROUP_CPU" ]; then
        if [ -f "$CGROUP_CPU/tasks" ]; then
            while read -r pid; do
                echo "$pid" > /sys/fs/cgroup/cpu/tasks 2>/dev/null || true
            done < "$CGROUP_CPU/tasks"
        fi
        rmdir "$CGROUP_CPU" 2>/dev/null || true
        echo "CPU cgroup 已移除"
    fi
    if [ -d "$CGROUP_MEM" ]; then
        if [ -f "$CGROUP_MEM/tasks" ]; then
            while read -r pid; do
                echo "$pid" > /sys/fs/cgroup/memory/tasks 2>/dev/null || true
            done < "$CGROUP_MEM/tasks"
        fi
        rmdir "$CGROUP_MEM" 2>/dev/null || true
        echo "内存 cgroup 已移除"
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
