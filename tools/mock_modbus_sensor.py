#!/usr/bin/env python3
"""
Modbus RTU 从站模拟器 — M1 阶段测试用
模拟一个 Modbus RTU 从站，返回虚拟温湿度传感器数据

用法:
  # 配合 socat 虚拟串口:
  socat -d -d pty,raw,echo=0 pty,raw,echo=0 &
  python3 tools/mock_modbus_sensor.py --port /dev/pts/X

  # 或直接连物理串口:
  python3 tools/mock_modbus_sensor.py --port /dev/ttyUSB0
"""

import sys
import time
import random
import struct
import argparse
import signal
import serial

# ── CRC16-Modbus (与 common/ModbusRtu.cpp 中 crc16_modbus 一致) ──
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
    """构建功能码 0x03 (Read Holding Registers) 响应帧"""
    byte_count = len(registers) * 2
    frame = bytearray()
    frame.append(slave_addr)
    frame.append(0x03)          # 功能码
    frame.append(byte_count)     # 数据字节数
    for reg in registers:
        frame.append((reg >> 8) & 0xFF)   # 高字节在前
        frame.append(reg & 0xFF)          # 低字节在后
    crc = crc16_modbus(bytes(frame))
    frame.append(crc & 0xFF)              # CRC 低字节在前
    frame.append((crc >> 8) & 0xFF)
    return bytes(frame)


def build_error_response(slave_addr: int, func_code: int, exception_code: int) -> bytes:
    """构建异常响应帧 (功能码 | 0x80)"""
    frame = bytearray()
    frame.append(slave_addr)
    frame.append(func_code | 0x80)
    frame.append(exception_code)
    crc = crc16_modbus(bytes(frame))
    frame.append(crc & 0xFF)
    frame.append((crc >> 8) & 0xFF)
    return bytes(frame)


def main():
    parser = argparse.ArgumentParser(description='Modbus RTU 从站模拟器')
    parser.add_argument('--port', required=True, help='串口路径, e.g. /dev/ttyUSB0 或 /dev/pts/3')
    parser.add_argument('--slave-id', type=int, default=1, help='从站地址 (默认: 1)')
    parser.add_argument('--baudrate', type=int, default=9600, help='波特率 (默认: 9600)')
    parser.add_argument('--reg-count', type=int, default=10, help='模拟的寄存器数量 (默认: 10)')
    args = parser.parse_args()

    running = True

    def signal_handler(sig, frame):
        nonlocal running
        running = False

    signal.signal(signal.SIGINT, signal_handler)
    signal.signal(signal.SIGTERM, signal_handler)

    # 打开串口
    ser = serial.Serial(
        args.port,
        baudrate=args.baudrate,
        timeout=0.5,
        bytesize=serial.EIGHTBITS,
        parity=serial.PARITY_NONE,
        stopbits=serial.STOPBITS_ONE,
    )

    print(f"[MockModbus] 从站地址=0x{args.slave_id:02X}, 端口={args.port}, "
          f"波特率={args.baudrate}, 寄存器数={args.reg_count}")

    # 每 2 秒微调一次传感器值，模拟真实波动
    last_update = time.time()

    # 寄存器值: reg[0]=温度(放大10倍), reg[1]=湿度(放大10倍), reg[2]=压力, 其余保留
    registers = [0] * args.reg_count
    registers[0] = 250   # 25.0°C
    registers[1] = 600   # 60.0%
    registers[2] = 1013  # 101.3 kPa (放大10倍)

    buf = bytearray()
    req_count = 0

    while running:
        # 2 秒更新一次模拟值
        now = time.time()
        if now - last_update >= 2.0:
            last_update = now
            registers[0] = 250 + random.randint(-5, 5)     # 24.5 ~ 25.5°C
            registers[1] = 600 + random.randint(-10, 10)   # 59.0 ~ 61.0%
            registers[2] = 1013 + random.randint(-3, 3)    # 101.0 ~ 101.6 kPa

        # 读取串口数据
        try:
            waiting = ser.in_waiting
            if waiting > 0:
                data = ser.read(waiting)
                buf.extend(data)
            else:
                time.sleep(0.05)
                continue
        except serial.SerialException as e:
            print(f"[MockModbus] 串口错误: {e}")
            break

        # 逐帧解析 (Modbus RTU 帧以 3.5 字符间隔分隔，这里简化处理)
        MIN_FRAME_LEN = 8  # addr+func+start(2)+qty(2)+crc(2)

        while len(buf) >= MIN_FRAME_LEN:
            # 跳过不匹配的从站地址
            if buf[0] != args.slave_id:
                buf.pop(0)
                continue

            func_code = buf[1]

            # 只支持功能码 0x03
            if func_code != 0x03:
                print(f"[MockModbus] 不支持的功能码: 0x{func_code:02X}, 跳过")
                buf = buf[MIN_FRAME_LEN:]  # 跳过当前帧
                continue

            # 解析请求
            start_addr = (buf[2] << 8) | buf[3]
            quantity = (buf[4] << 8) | buf[5]

            # 验证 CRC (覆盖前 6 字节)
            expected_crc = crc16_modbus(bytes(buf[:6]))
            actual_crc = buf[6] | (buf[7] << 8)
            if expected_crc != actual_crc:
                # CRC 不匹配，可能是帧未对齐，跳过 1 字节重试
                buf.pop(0)
                continue

            # 帧合法，构建响应
            req_count += 1

            if quantity == 0 or quantity > 125:
                # Modbus 标准: 一次最多读 125 个寄存器
                resp = build_error_response(args.slave_id, 0x03, 0x03)  # 非法数据值
            else:
                # 构造响应寄存器列表
                resp_regs = []
                for i in range(quantity):
                    addr = start_addr + i
                    if addr < args.reg_count:
                        resp_regs.append(registers[addr])
                    else:
                        resp_regs.append(0)
                resp = build_read_holding_response(args.slave_id, resp_regs)

            ser.write(resp)

            if req_count % 20 == 1:
                print(f"[MockModbus] 第 {req_count} 次响应: "
                      f"start={start_addr}, qty={quantity}, "
                      f"temp={registers[0]/10:.1f}°C, "
                      f"hum={registers[1]/10:.1f}%, "
                      f"press={registers[2]/10:.1f}kPa")

            # 消费已处理的 8 字节帧头
            buf = buf[MIN_FRAME_LEN:]
            break  # 处理完一帧后重新进入外层循环

    ser.close()
    print(f"[MockModbus] 停止 (共响应 {req_count} 次请求)")


if __name__ == '__main__':
    main()
