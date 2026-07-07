#!/bin/bash
# M1 全链路启动脚本
# 用法: bash scripts/start_gateway.sh

set -e

PROJECT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
cd "$PROJECT_DIR"

echo "============================================"
echo "  AIoT 边缘网关 M1 全链路启动"
echo "  项目目录: $PROJECT_DIR"
echo "============================================"

# ── Step 1: 清理上次残留 ──
echo "[1/5] 清理残留进程..."
pkill -f "gateway_core"     2>/dev/null || true
pkill -f "gateway_access"   2>/dev/null || true
pkill -f "gateway_monitor"  2>/dev/null || true
pkill -f "gateway_engine"   2>/dev/null || true
pkill -f "mock_modbus_sensor" 2>/dev/null || true
pkill -f "socat.*vmodbus"   2>/dev/null || true
rm -f /tmp/gateway_data.sock /tmp/gateway_engine.sock /tmp/gateway_data.db
sleep 1

# ── Step 2: 创建虚拟串口对 ──
echo "[2/5] 创建虚拟串口对 (socat)..."
socat -d -d pty,raw,echo=0,link=/tmp/vmodbus_master pty,raw,echo=0,link=/tmp/vmodbus_slave &
SOCAT_PID=$!
sleep 1
echo "  主端: /tmp/vmodbus_master -> 从端: /tmp/vmodbus_slave"

# ── Step 3: 启动 Modbus 从站模拟器 ──
echo "[3/5] 启动 Modbus 从站模拟器..."
python3 "$PROJECT_DIR/tools/mock_modbus_sensor.py" \
    --port /tmp/vmodbus_slave \
    --slave-id 1 \
    --baudrate 9600 \
    --reg-count 10 &
MOCK_PID=$!
sleep 0.5
echo "  Mock PID=$MOCK_PID"

# ── Step 4: 编译项目 ──
echo "[4/5] 编译项目..."
mkdir -p "$PROJECT_DIR/build"
cd "$PROJECT_DIR/build"
cmake .. -DCMAKE_BUILD_TYPE=Debug 2>&1 | tail -3
make -j$(nproc) 2>&1 | tail -5
cd "$PROJECT_DIR"
echo "  编译完成"

# ── Step 5: 按依赖顺序启动进程 ──
echo "[5/5] 启动网关进程..."
cd "$PROJECT_DIR"

# B: gateway_core (先启动，UDS server 监听)
./build/gateway_core/gateway_core &
CORE_PID=$!
echo "  gateway_core   PID=$CORE_PID"
sleep 0.5

# A: gateway_access (连接 B，使用虚拟串口主端)
# 注意: gateway_access 硬编码了 /dev/ttyUSB0，这里需要临时修改或使用符号链接
# 如果 /tmp/vmodbus_master 已存在，创建符号链接(需要 sudo)
if [ ! -e /dev/ttyUSB0 ]; then
    echo "  [提示] 需要 sudo 创建串口符号链接:"
    echo "    sudo ln -sf /tmp/vmodbus_master /dev/ttyUSB0"
    echo "  或者手动将 gateway_access/main.cpp 中的 /dev/ttyUSB0 改为 /tmp/vmodbus_master 后重新编译"
    echo ""
    # 尝试创建符号链接
    sudo ln -sf /tmp/vmodbus_master /dev/ttyUSB0 2>/dev/null && echo "  符号链接已创建: /dev/ttyUSB0 -> /tmp/vmodbus_master" || echo "  符号链接创建失败，请手动处理"
fi

./build/gateway_access/gateway_access &
ACCESS_PID=$!
echo "  gateway_access  PID=$ACCESS_PID"
sleep 0.5

# C: gateway_monitor (Qt 监控台，最后一个启动)
./build/gateway_monitor/gateway_monitor &
MONITOR_PID=$!
echo "  gateway_monitor PID=$MONITOR_PID"

# ── 汇总 ──
echo ""
echo "============================================"
echo "  全链路启动完成"
echo "============================================"
echo "  gateway_core    PID=$CORE_PID"
echo "  gateway_access  PID=$ACCESS_PID"
echo "  gateway_monitor PID=$MONITOR_PID"
echo "  mock_modbus     PID=$MOCK_PID"
echo "  socat           PID=$SOCAT_PID"
echo ""
echo "  查看日志: tail -f logs/*.log"
echo "  停止全部: bash scripts/stop_gateway.sh"
echo "============================================"

# 前台等待，方便 Ctrl+C 全部终止
echo "按 Ctrl+C 停止所有进程..."
wait
