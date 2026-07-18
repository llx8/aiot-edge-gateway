#!/bin/bash
# AIoT 边缘网关 — 全链路启动脚本
# 用法: bash scripts/start_gateway.sh [--nobuild]

PROJECT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
cd "$PROJECT_DIR"

echo "============================================"
echo "  AIoT 边缘网关 全链路启动"
echo "============================================"

# ── 1. 清理残留 ──
echo "[1/9] 清理残留进程..."
pkill -f "build/gateway_" 2>/dev/null || true
pkill -f "mock_modbus" 2>/dev/null || true
pkill -f "socat.*vmodbus" 2>/dev/null || true
pkill -f "Xvfb" 2>/dev/null || true
pkill -f "x11vnc" 2>/dev/null || true
sleep 1
rm -f /tmp/gateway_data.sock /tmp/gateway_engine.sock /tmp/gateway_monitor.sock /tmp/vmodbus_*
echo "      清理完成"

# ── 2. 清理旧 VNC ──
echo "[2/9] 清理旧 VNC 进程..."
pkill -f "Xvfb.*:99" 2>/dev/null || true
pkill -f "x11vnc.*:99" 2>/dev/null || true
sleep 0.3

# ── 3. 启动 VNC (Xvfb + x11vnc) ──
echo "[3/9] 启动 VNC 服务..."
if [ -z "$DISPLAY" ] || [ ! -S "/tmp/.X11-unix/X${DISPLAY#:}" ]; then
    Xvfb :99 -screen 0 1280x720x24 -ac +extension GLX > /dev/null 2>&1 &
    sleep 0.5
    x11vnc -display :99 -forever -nopw -quiet -rfbport 5900 -wait 5 -defer 5 -nocursor > /dev/null 2>&1 &
    export DISPLAY=:99
    echo "      虚拟桌面 DISPLAY=:99, VNC 端口 5900"
else
    echo "      使用已有 DISPLAY=$DISPLAY"
    if ! pgrep -f "x11vnc.*${DISPLAY#:}" > /dev/null 2>&1; then
        x11vnc -display "$DISPLAY" -forever -nopw -quiet -rfbport 5900 -wait 5 -defer 5 -nocursor > /dev/null 2>&1 &
        echo "      x11vnc 已附加到 $DISPLAY, 端口 5900"
    fi
fi

# ── 4. 编译（可选） ──
if [[ "$1" != "--nobuild" ]]; then
    echo "[4/9] 编译..."
    mkdir -p build
    cd build
    cmake .. -DCMAKE_BUILD_TYPE=Debug > /dev/null 2>&1
    make -j$(nproc) 2>&1 | tail -3
    cd "$PROJECT_DIR"
else
    echo "[4/9] 跳过编译 (--nobuild)"
fi

# ── 4. Modbus RTU 模拟器 ──
echo "[5/9] 启动 Modbus RTU 模拟器..."
socat pty,raw,echo=0,link=/tmp/vmodbus_master pty,raw,echo=0,link=/tmp/vmodbus_slave > /dev/null 2>&1 &
sleep 1

python3 -u "$PROJECT_DIR/tools/mock_modbus_sensor.py" \
    --port /tmp/vmodbus_slave --slave-id 1 --baudrate 9600 --reg-count 10 \
    > /tmp/mock_rtu.log 2>&1 &
sleep 0.5

sudo ln -sf /tmp/vmodbus_master /dev/ttyUSB0 2>/dev/null || true

# ── 5. cgroups 资源隔离 ──
echo "[6/9] 设置 cgroups 资源隔离..."
sudo bash "$PROJECT_DIR/scripts/setup_cgroups.sh" on 2>&1 | sed 's/^/      /'
sleep 0.5

# ── 6. Modbus TCP 模拟器（须早于 Watchdog，否则 access 连接 5020 时 mock 未就绪）──
echo "[7/9] 启动 Modbus TCP 模拟器..."
python3 -u "$PROJECT_DIR/tools/mock_modbus_tcp_sensor.py" --port 5020 \
    > /tmp/mock_tcp.log 2>&1 &
sleep 1

# ── 7. 启动 Watchdog ──
echo "[8/9] 启动 Watchdog..."
nohup ./build/gateway_watchdog/gateway_watchdog > /tmp/watchdog.log 2>&1 &
sleep 2

# ── 8. 等待就绪 + 触发 AI ──
echo "[9/9] 等待就绪并触发 AI 分析..."
for i in $(seq 1 15); do
    if [ -S /tmp/gateway_data.sock ] && [ -S /tmp/gateway_engine.sock ]; then
        echo "      网关就绪 (${i}s)"
        break
    fi
    sleep 1
done

sleep 2
mosquitto_pub -t "spBv1.0/edge_gateway/DCMD/gw001" \
    -m '{"method":"start_analysis","camera":"zone_A","model":"yolov5s.rknn"}' \
    2>/dev/null && echo "      AI 分析已启动" || echo "      警告: START_ANALYSIS 发送失败"

# ── cgroup: 将 engine 进程加入资源限制 ──
ENGINE_CGROUP="/sys/fs/cgroup/user.slice/user-1000.slice/gateway_engine"
if [ -f "$ENGINE_CGROUP/cpu.max" ]; then
    ENGINE_PID=$(pgrep -f "gateway_engine$" 2>/dev/null)
    if [ -n "$ENGINE_PID" ]; then
        echo "$ENGINE_PID" | sudo tee "$ENGINE_CGROUP/cgroup.procs" > /dev/null 2>&1 && \
            echo "      engine (PID $ENGINE_PID) 已加入 cgroup 限制"
    fi
fi

echo ""
echo "============================================"
echo "  启动完成"
echo "============================================"
echo "  Qt 仪表盘:    VNC 桌面"
echo "  HTTP 仪表盘:  http://127.0.0.1:8081"
echo "  查看日志:     tail -f logs/*.log"
echo "  停止:         bash scripts/stop_gateway.sh"
echo "============================================"
