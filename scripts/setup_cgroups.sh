#!/bin/bash
# cgroups v2 资源隔离 — 限制 gateway_engine CPU ≤ 60%、内存 ≤ 2GB
# 策略: 在 user-1000.slice 下建子 cgroup（与 session scope 同级）
# 用法: sudo bash scripts/setup_cgroups.sh [on|off]
set -e

CGROUP_USER="${SUDO_USER:-ubuntu}"
# 引擎 cgroup 路径固定在 user-1000.slice 下
CGROUP_DIR="/sys/fs/cgroup/user.slice/user-1000.slice/gateway_engine"

apply_limits() {
    local parent="/sys/fs/cgroup/user.slice/user-1000.slice"

    # 让父 cgroup 允许子 cgroup 使用 cpu/memory 控制器
    echo "+cpu +memory" > "$parent/cgroup.subtree_control" 2>/dev/null || {
        echo "  警告: 无法修改 subtree_control，可能是 systemd 限制" >&2
    }

    if [ ! -d "$CGROUP_DIR" ]; then
        mkdir -p "$CGROUP_DIR"
    fi

    # CPU: 60%
    echo "60000 100000" > "$CGROUP_DIR/cpu.max" 2>/dev/null
    echo "  CPU:   ≤ 60%"

    # 内存: 2GB
    echo "2147483648" > "$CGROUP_DIR/memory.max" 2>/dev/null
    echo "  内存:  ≤ 2GB"

    # 授权引擎进程自行加入
    chown "$CGROUP_USER:$CGROUP_USER" "$CGROUP_DIR/cgroup.procs" 2>/dev/null || \
    chmod 666 "$CGROUP_DIR/cgroup.procs" 2>/dev/null || true
    echo "  cgroup: $CGROUP_DIR"
}

remove_limits() {
    if [ -d "$CGROUP_DIR" ]; then
        local parent="/sys/fs/cgroup/user.slice/user-1000.slice"
        if [ -f "$CGROUP_DIR/cgroup.procs" ]; then
            while read -r pid; do
                echo "$pid" > "$parent/cgroup.procs" 2>/dev/null || true
            done < "$CGROUP_DIR/cgroup.procs"
        fi
        rmdir "$CGROUP_DIR" 2>/dev/null || true
        echo "  cgroup gateway_engine 已移除"
    fi
}

case "${1:-on}" in
    on|start)  apply_limits ;;
    off|stop)  remove_limits ;;
    *)         echo "用法: $0 [on|off]" >&2; exit 1 ;;
esac
