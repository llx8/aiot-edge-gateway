#!/usr/bin/env python3
"""
Modbus TCP 从站模拟器 — 多场景复合数据源
用法: python3 tools/mock_modbus_tcp_sensor.py --port 5020 --slave-id 1
"""

import sys
import time
import random
import math
import argparse
import signal
import socket

def build_tcp_response(trans_id: int, unit_id: int, registers: list) -> bytes:
    byte_count = len(registers) * 2
    frame = bytearray()
    frame.append((trans_id >> 8) & 0xFF)
    frame.append(trans_id & 0xFF)
    frame.append(0x00)
    frame.append(0x00)
    length = 1 + 1 + 1 + byte_count
    frame.append((length >> 8) & 0xFF)
    frame.append(length & 0xFF)
    frame.append(unit_id)
    frame.append(0x03)
    frame.append(byte_count)
    for reg in registers:
        frame.append((reg >> 8) & 0xFF)
        frame.append(reg & 0xFF)
    return bytes(frame)

def handle_request(data: bytes, registers: list, reg_count: int, slave_id: int) -> bytes | None:
    if len(data) < 12:
        return None
    trans_id = (data[0] << 8) | data[1]
    unit_id = data[6]
    func_code = data[7]
    if unit_id != slave_id or func_code != 0x03:
        return None
    start_addr = (data[8] << 8) | data[9]
    quantity = (data[10] << 8) | data[11]
    if quantity == 0 or quantity > 125:
        return None
    resp_regs = [registers[start_addr + i] if start_addr + i < reg_count else 0 for i in range(quantity)]
    return build_tcp_response(trans_id, unit_id, resp_regs)

def main():
    parser = argparse.ArgumentParser(description='Modbus TCP 从站模拟器(多场景)')
    parser.add_argument('--port', type=int, default=502)
    parser.add_argument('--slave-id', type=int, default=1)
    parser.add_argument('--reg-count', type=int, default=10)
    args = parser.parse_args()

    running = True
    def signal_handler(sig, frame):
        nonlocal running
        running = False
    signal.signal(signal.SIGINT, signal_handler)
    signal.signal(signal.SIGTERM, signal_handler)

    server = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    server.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    server.bind(('127.0.0.1', args.port))
    server.listen(1)
    server.settimeout(1.0)

    print(f"[MockModbusTCP] slave=0x{args.slave_id:02X} port={args.port}")

    registers = [0] * args.reg_count
    registers[0] = 250
    registers[1] = 600
    registers[2] = 1013

    req_count = 0
    conn = None
    start_time = time.time()

    spike_next = start_time + random.uniform(20, 30)
    spike_until = 0.0
    spike_temp = 0.0
    motor_cycle = 120.0
    motor_ramp = 30.0
    motor_cool = 15.0

    while running:
        now = time.time()
        elapsed = now - start_time

        # ── 正弦 + 启停 + 尖峰 ──
        temp = 27.0 + 5.0 * math.sin(elapsed / 60.0 * 2 * math.pi)

        phase = elapsed % motor_cycle
        if phase < motor_ramp:
            temp += 28.0 * (phase / motor_ramp)
        elif phase < motor_ramp + 10:
            temp += 28.0
        elif phase < motor_ramp + 10 + motor_cool:
            cool = phase - motor_ramp - 10
            temp += 28.0 * (1.0 - cool / motor_cool)

        if spike_until > 0 and now < spike_until:
            temp = spike_temp
        elif now >= spike_next:
            spike_until = now + 2.0
            spike_temp = random.uniform(82.0, 88.0)
            spike_next = now + random.uniform(40, 60)
            temp = spike_temp
            print(f"[MockModbusTCP] ⚡ SPIKE: {temp:.1f}°C", file=sys.stderr)

        hum = 70.0 - (temp - 22.0) * 1.5 + random.uniform(-2, 2)
        hum = max(30, min(90, hum))

        registers[0] = max(0, int(temp * 10))
        registers[1] = max(0, int(hum * 10))
        registers[2] = 1013 + random.randint(-3, 3)

        if conn is None:
            try:
                conn, addr = server.accept()
                conn.settimeout(1.0)
                print(f"[MockModbusTCP] client connected: {addr}")
            except socket.timeout:
                continue
            except OSError:
                break

        try:
            data = conn.recv(256)
            if not data:
                conn.close()
                conn = None
                continue
        except socket.timeout:
            continue
        except OSError:
            break

        resp = handle_request(data, registers, args.reg_count, args.slave_id)
        if resp is not None:
            conn.sendall(resp)
            req_count += 1
            if req_count % 50 == 1:
                print(f"[MockModbusTCP] #{req_count} temp={temp:.1f}°C hum={hum:.1f}%")

    if conn:
        conn.close()
    server.close()
    print(f"[MockModbusTCP] 停止 ({req_count} 次响应)")

if __name__ == '__main__':
    main()
