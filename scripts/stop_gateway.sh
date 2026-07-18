#!/bin/bash
# AIoT 边缘网关 — 停止脚本

echo "正在停止所有网关进程..."

for name in gateway_monitor gateway_access gateway_engine gateway_core gateway_watchdog mock_modbus socat Xvfb x11vnc; do
    pkill -f "$name" 2>/dev/null && echo "  $name 已停止" || true
done

sleep 0.5
pkill -9 -f "build/gateway_" 2>/dev/null || true
pkill -9 -f "mock_modbus" 2>/dev/null || true
pkill -9 -f "socat.*vmodbus" 2>/dev/null || true

rm -f /tmp/gateway_data.sock /tmp/gateway_engine.sock /tmp/gateway_monitor.sock
rm -f /tmp/vmodbus_master /tmp/vmodbus_slave
rm -f /tmp/mock_rtu.log /tmp/mock_tcp.log /tmp/watchdog.log /tmp/socat.log
rm -f /tmp/mock_rtu.pid /tmp/mock_tcp.pid /tmp/watchdog.pid /tmp/socat.pid

echo "全部已停止"
