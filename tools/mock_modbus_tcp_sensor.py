#!/usr/bin/env python3
"""
Modbus TCP 从站模拟器 — M2 阶段测试用
模拟一个 Modbus TCP 从站 (监听 502 端口)，返回虚拟温湿度传感器数据

用法:
  python3 tools/mock_modbus_tcp_sensor.py --port 502
  python3 tools/mock_modbus_tcp_sensor.py --port 5020 --slave-id 5

与 mock_modbus_sensor.py 的差异:
  - 传输层: TCP socket 替代串口
  - 帧结构: MBAP 头 (7B) + PDU，无 CRC
"""

import sys
import time
import random
import struct
import argparse
import signal
import socket


def build_tcp_response(trans_id: int, unit_id: int, registers: list) -> bytes:
    """构建 Modbus TCP 功能码 0x03 响应帧 (MBAP头 + PDU, 无CRC)"""
    byte_count = len(registers) * 2
    frame = bytearray()
    # MBAP 头: 事务ID(2) + 协议ID(2) + 长度(2)
    frame.append((trans_id >> 8) & 0xFF)
    frame.append(trans_id & 0xFF)
    frame.append(0x00)  # 协议ID 高
    frame.append(0x00)  # 协议ID 低
    # 长度 = 单元ID(1) + 功能码(1) + 字节数(1) + 寄存器数据
    length = 1 + 1 + 1 + byte_count
    frame.append((length >> 8) & 0xFF)
    frame.append(length & 0xFF)
    # 单元ID + PDU
    frame.append(unit_id)
    frame.append(0x03)           # 功能码
    frame.append(byte_count)      # 数据字节数
    for reg in registers:
        frame.append((reg >> 8) & 0xFF)   # 高字节在前
        frame.append(reg & 0xFF)          # 低字节在后
    return bytes(frame)


def build_tcp_error_response(trans_id: int, unit_id: int, func_code: int, exception_code: int) -> bytes:
    """构建 Modbus TCP 异常响应帧"""
    frame = bytearray()
    # MBAP 头
    frame.append((trans_id >> 8) & 0xFF)
    frame.append(trans_id & 0xFF)
    frame.append(0x00)
    frame.append(0x00)
    frame.append(0x00)
    frame.append(0x03)            # 长度 = 单元ID(1) + 功能码(1) + 异常码(1) = 3
    # 单元ID + PDU
    frame.append(unit_id)
    frame.append(func_code | 0x80)
    frame.append(exception_code)
    return bytes(frame)


def handle_request(data: bytes, registers: list, reg_count: int, slave_id: int) -> bytes | None:
    """解析 Modbus TCP 请求帧，返回响应帧或 None"""
    if len(data) < 12:
        return None

    # 解析 MBAP 头
    trans_id = (data[0] << 8) | data[1]
    # 协议ID、长度 跳过不校验
    unit_id = data[6]
    func_code = data[7]

    if unit_id != slave_id:
        return None   # 不是发给我的帧

    if func_code != 0x03:
        return build_tcp_error_response(trans_id, unit_id, 0x03, 0x01)  # 非法功能码

    start_addr = (data[8] << 8) | data[9]
    quantity = (data[10] << 8) | data[11]

    if quantity == 0 or quantity > 125:
        return build_tcp_error_response(trans_id, unit_id, 0x03, 0x03)  # 非法数据值

    # 构造响应寄存器列表
    resp_regs = []
    for i in range(quantity):
        addr = start_addr + i
        if addr < reg_count:
            resp_regs.append(registers[addr])
        else:
            resp_regs.append(0)

    return build_tcp_response(trans_id, unit_id, resp_regs)


def main():
    parser = argparse.ArgumentParser(description='Modbus TCP 从站模拟器')
    parser.add_argument('--port', type=int, default=502, help='监听端口 (默认: 502)')
    parser.add_argument('--slave-id', type=int, default=1, help='从站地址/单元ID (默认: 1)')
    parser.add_argument('--reg-count', type=int, default=10, help='模拟的寄存器数量 (默认: 10)')
    args = parser.parse_args()

    running = True

    def signal_handler(sig, frame):
        nonlocal running
        running = False

    signal.signal(signal.SIGINT, signal_handler)
    signal.signal(signal.SIGTERM, signal_handler)

    # 创建 TCP 服务端 socket
    server = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    server.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    server.bind(('127.0.0.1', args.port))
    server.listen(1)
    server.settimeout(1.0)  # 1秒超时，便于检测退出信号

    print(f"[MockModbusTCP] 从站地址=0x{args.slave_id:02X}, 端口={args.port}, "
          f"寄存器数={args.reg_count}")

    # 初始寄存器值: reg[0]=温度(放大10倍), reg[1]=湿度(放大10倍), reg[2]=压力
    registers = [0] * args.reg_count
    registers[0] = 250   # 25.0°C
    registers[1] = 600   # 60.0%
    registers[2] = 1013  # 101.3 kPa

    last_update = time.time()
    req_count = 0
    conn = None

    while running:
        # 2 秒更新一次模拟值
        now = time.time()
        if now - last_update >= 2.0:
            last_update = now
            registers[0] = 250 + random.randint(-5, 5)     # 24.5 ~ 25.5°C
            registers[1] = 600 + random.randint(-10, 10)   # 59.0 ~ 61.0%
            registers[2] = 1013 + random.randint(-3, 3)    # 101.0 ~ 101.6 kPa

        # 等待客户端连接
        if conn is None:
            try:
                conn, addr = server.accept()
                conn.settimeout(1.0)
                print(f"[MockModbusTCP] 客户端连接: {addr}")
            except socket.timeout:
                continue
            except OSError:
                break

        # 读取请求
        try:
            data = conn.recv(256)
            if not data:
                print(f"[MockModbusTCP] 客户端断开")
                conn.close()
                conn = None
                continue
        except socket.timeout:
            continue
        except OSError:
            break

        # 处理请求
        resp = handle_request(data, registers, args.reg_count, args.slave_id)
        if resp is not None:
            conn.sendall(resp)
            req_count += 1

            if req_count % 20 == 1:
                start_addr = (data[8] << 8) | data[9]
                quantity = (data[10] << 8) | data[11]
                print(f"[MockModbusTCP] 第 {req_count} 次响应: "
                      f"start={start_addr}, qty={quantity}, "
                      f"temp={registers[0]/10:.1f}°C, "
                      f"hum={registers[1]/10:.1f}%, "
                      f"press={registers[2]/10:.1f}kPa")

    if conn:
        conn.close()
    server.close()
    print(f"[MockModbusTCP] 停止 (共响应 {req_count} 次请求)")


if __name__ == '__main__':
    main()
