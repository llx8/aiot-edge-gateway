#!/bin/bash
# 停止所有网关进程

echo "正在停止所有网关进程..."

# 优雅终止
pkill -f "gateway_monitor"  2>/dev/null && echo "  gateway_monitor 已停止" || true
pkill -f "gateway_access"   2>/dev/null && echo "  gateway_access 已停止" || true
pkill -f "gateway_engine"   2>/dev/null && echo "  gateway_engine 已停止" || true
pkill -f "gateway_core"     2>/dev/null && echo "  gateway_core 已停止" || true
pkill -f "mock_modbus_sensor" 2>/dev/null && echo "  mock_modbus 已停止" || true
pkill -f "socat.*vmodbus"   2>/dev/null && echo "  socat(vmodbus) 已停止" || true

sleep 0.5

# 强制清理可能残留的
pkill -9 -f "gateway_"      2>/dev/null || true
pkill -9 -f "mock_modbus"   2>/dev/null || true
pkill -9 -f "socat.*vmodbus" 2>/dev/null || true

# 清理临时文件
rm -f /tmp/gateway_data.sock /tmp/gateway_engine.sock /tmp/gateway_data.db
rm -f /tmp/vmodbus_master /tmp/vmodbus_slave

echo "全部进程已停止，临时文件已清理"
