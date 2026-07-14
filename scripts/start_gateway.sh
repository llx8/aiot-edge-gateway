#!/bin/bash
set -e

PROJECT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
cd "$PROJECT_DIR"

echo "============================================"
echo "  AIoT 边缘网关 全链路启动"
echo "  项目目录: $PROJECT_DIR"
echo "============================================"

echo "[1/6] 清理残留进程..."
pkill -f "gateway_core"     2>/dev/null || true
pkill -f "gateway_access"   2>/dev/null || true
pkill -f "gateway_monitor"  2>/dev/null || true
pkill -f "gateway_engine"   2>/dev/null || true
pkill -f "gateway_watchdog" 2>/dev/null || true
pkill -f "mock_modbus"      2>/dev/null || true
pkill -f "socat.*vmodbus"   2>/dev/null || true
rm -f /tmp/gateway_data.sock /tmp/gateway_engine.sock /tmp/gateway_monitor.sock /tmp/gateway_data.db
sleep 1

echo "[2/6] 创建虚拟串口对..."
socat -d -d pty,raw,echo=0,link=/tmp/vmodbus_master pty,raw,echo=0,link=/tmp/vmodbus_slave &
SOCAT_PID=$!
sleep 1

echo "[3/6] 启动 Modbus 从站模拟器..."
python3 "$PROJECT_DIR/tools/mock_modbus_sensor.py" \
    --port /tmp/vmodbus_slave \
    --slave-id 1 \
    --baudrate 9600 \
    --reg-count 10 &
MOCK_PID=$!
sleep 0.5

echo "[4/6] 编译项目..."
mkdir -p "$PROJECT_DIR/build"
cd "$PROJECT_DIR/build"
cmake .. -DCMAKE_BUILD_TYPE=Debug 2>&1 | tail -3
make -j$(nproc) 2>&1 | tail -5
cd "$PROJECT_DIR"

echo "[5/6] 启动 Watchdog（守护所有子进程）..."
sudo ln -sf /tmp/vmodbus_master /dev/ttyUSB0 2>/dev/null || true

./build/gateway_watchdog/gateway_watchdog &
WATCHDOG_PID=$!
sleep 2

echo "[6/6] 启动 Modbus TCP 从站模拟器..."
python3 "$PROJECT_DIR/tools/mock_modbus_tcp_sensor.py" &
MOCK_TCP_PID=$!

echo ""
echo "============================================"
echo "  全链路启动完成"
echo "============================================"
echo "  gateway_watchdog PID=$WATCHDOG_PID"
echo "  (watchdog 已启动 gateway_core/engine/access/monitor)"
echo "  mock_modbus_rtu  PID=$MOCK_PID"
echo "  mock_modbus_tcp  PID=$MOCK_TCP_PID"
echo "  socat             PID=$SOCAT_PID"
echo ""
echo "  查看日志: tail -f logs/*.log"
echo "  停止全部: bash scripts/stop_gateway.sh"
echo "============================================"

echo "按 Ctrl+C 停止 watchdog 及所有子进程..."
wait $WATCHDOG_PID
