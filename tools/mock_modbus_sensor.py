#!/usr/bin/env python3
"""
Modbus RTU 从站模拟器 — 多场景复合数据源
- 正弦日周期波动（22~32°C, 周期 60s）
- 随机异常尖峰（80~95°C, 间隔 20~30s）
- 模拟电机启停循环（25→55°C 爬坡再回落）

用法:
  socat -d -d pty,raw,echo=0,link=/tmp/vmodbus_master pty,raw,echo=0,link=/tmp/vmodbus_slave &
  python3 tools/mock_modbus_sensor.py --port /tmp/vmodbus_slave --slave-id 1
"""

import sys
import time
import random
import math
import argparse
import signal
import serial

def crc16_modbus(data: bytes) -> int:
    crc = 0xFFFF
    for byte in data:
        crc ^= byte
        for _ in range(8):
            if crc & 1:
                crc = (crc >> 1) ^ 0xA001
            else:
                crc >>= 1
    return crc

def build_read_holding_response(slave_addr: int, registers: list) -> bytes:
    byte_count = len(registers) * 2
    frame = bytearray()
    frame.append(slave_addr)
    frame.append(0x03)
    frame.append(byte_count)
    for reg in registers:
        frame.append((reg >> 8) & 0xFF)
        frame.append(reg & 0xFF)
    crc = crc16_modbus(bytes(frame))
    frame.append(crc & 0xFF)
    frame.append((crc >> 8) & 0xFF)
    return bytes(frame)

def main():
    parser = argparse.ArgumentParser(description='Modbus RTU 从站模拟器(多场景)')
    parser.add_argument('--port', required=True)
    parser.add_argument('--slave-id', type=int, default=1)
    parser.add_argument('--baudrate', type=int, default=9600)
    parser.add_argument('--reg-count', type=int, default=10)
    parser.add_argument('--scenario', type=str, default='all',
                        choices=['all','sine','spike','motor'],
                        help='场景: all=全部, sine=正弦, spike=尖峰, motor=启停')
    args = parser.parse_args()

    running = True
    def signal_handler(sig, frame):
        nonlocal running
        running = False
    signal.signal(signal.SIGINT, signal_handler)
    signal.signal(signal.SIGTERM, signal_handler)

    ser = serial.Serial(args.port, baudrate=args.baudrate, timeout=0.5,
                        bytesize=serial.EIGHTBITS, parity=serial.PARITY_NONE,
                        stopbits=serial.STOPBITS_ONE)
    print(f"[MockModbus] slave=0x{args.slave_id:02X} port={args.port} scenario={args.scenario}")

    registers = [0] * args.reg_count
    registers[0] = 250   # 25.0°C
    registers[1] = 600   # 60.0%
    registers[2] = 1013  # 101.3 kPa

    buf = bytearray()
    req_count = 0
    start_time = time.time()

    # 场景状态
    spike_next = start_time + random.uniform(20, 30)
    spike_until = 0.0
    spike_temp = 0.0

    motor_cycle = 120.0       # 120s 一个启停周期
    motor_ramp = 30.0         # 30s 爬坡
    motor_cool = 15.0         # 15s 回落

    while running:
        now = time.time()
        elapsed = now - start_time

        # ── 计算当前温度 ──
        temp = 27.0

        # 1. 正弦日波动 (周期 60s, 振幅 5°C)
        if args.scenario in ('all', 'sine'):
            temp += 5.0 * math.sin(elapsed / 60.0 * 2 * math.pi)

        # 2. 电机启停循环
        if args.scenario in ('all', 'motor'):
            phase = elapsed % motor_cycle
            if phase < motor_ramp:
                temp += 28.0 * (phase / motor_ramp)  # 25→53°C 叠加
            elif phase < motor_ramp + 10:
                temp += 28.0                    # 保持 53°C
            elif phase < motor_ramp + 10 + motor_cool:
                cool_phase = phase - motor_ramp - 10
                temp += 28.0 * (1.0 - cool_phase / motor_cool)
            # 其余时间无叠加

        # 3. 随机尖峰
        if args.scenario in ('all', 'spike'):
            if spike_until > 0 and now < spike_until:
                temp = spike_temp
            elif now >= spike_next:
                spike_until = now + 2.0
                spike_temp = random.uniform(82.0, 88.0)
                spike_next = now + random.uniform(40, 60)
                temp = spike_temp
                print(f"[MockModbus] ⚡ SPIKE: {temp:.1f}°C", file=sys.stderr)

        # 湿度与温度反向
        hum = 70.0 - (temp - 22.0) * 1.5 + random.uniform(-2, 2)
        hum = max(30, min(90, hum))

        registers[0] = max(0, int(temp * 10))
        registers[1] = max(0, int(hum * 10))
        registers[2] = 1013 + random.randint(-3, 3)

        # ── 读取并响应 ──
        try:
            waiting = ser.in_waiting
            if waiting > 0:
                buf.extend(ser.read(waiting))
            else:
                time.sleep(0.02)
                continue
        except serial.SerialException as e:
            print(f"[MockModbus] 串口错误: {e}", file=sys.stderr)
            time.sleep(0.1)
            continue

        MIN_FRAME_LEN = 8
        while len(buf) >= MIN_FRAME_LEN:
            if buf[0] != args.slave_id:
                buf.pop(0)
                continue
            if buf[1] != 0x03:
                buf = buf[MIN_FRAME_LEN:]
                continue

            start_addr = (buf[2] << 8) | buf[3]
            quantity = (buf[4] << 8) | buf[5]
            expected_crc = crc16_modbus(bytes(buf[:6]))
            actual_crc = buf[6] | (buf[7] << 8)
            if expected_crc != actual_crc:
                buf.pop(0)
                continue

            req_count += 1
            resp_regs = []
            for i in range(quantity):
                addr = start_addr + i
                resp_regs.append(registers[addr] if addr < args.reg_count else 0)
            resp = build_read_holding_response(args.slave_id, resp_regs)
            ser.write(resp)

            if req_count % 50 == 1:
                print(f"[MockModbus] #{req_count} temp={temp:.1f}°C hum={hum:.1f}%")

            buf = buf[MIN_FRAME_LEN:]
            break

    ser.close()
    print(f"[MockModbus] 停止 ({req_count} 次响应)")

if __name__ == '__main__':
    main()
