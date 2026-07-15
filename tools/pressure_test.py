#!/usr/bin/env python3
"""
pressure_test.py — 高并发压测脚本

用途：模拟多路 RTSP + Modbus 并发场景，验证网关在高负载下的稳定性。

用法：
  # 仅 MQTT 压测（无需 Modbus 从站）
  python3 pressure_test.py --mqtt --duration 300

  # 仅 Modbus 压测（需要从站模拟器运行）
  python3 pressure_test.py --modbus --duration 300

  # 全压测（MQTT + Modbus 并发）
  python3 pressure_test.py --all --duration 300

  # 自定义参数
  python3 pressure_test.py --all --duration 600 --mqtt-rate 50 --modbus-devices 10

依赖：
  pip install paho-mqtt minimalmodbus
"""

import argparse
import json
import time
import random
import threading
import sys
import os
from datetime import datetime

# ── MQTT 压测 ──

class MqttLoadTester:
    """模拟云端下发 RPC 指令 + 订阅告警，统计消息吞吐"""

    def __init__(self, broker="localhost", port=1883, rate=20):
        self.broker = broker
        self.port = port
        self.rate = rate  # 每秒下发指令数
        self.sent = 0
        self.received = 0
        self.errors = 0
        self.running = False
        self.client = None

    def start(self):
        import paho.mqtt.client as mqtt

        self.client = mqtt.Client(client_id=f"pressure_test_{random.randint(1000,9999)}")
        self.client.on_connect = self._on_connect
        self.client.on_message = self._on_message

        self.client.connect(self.broker, self.port, 60)
        self.client.loop_start()

        self.client.subscribe("spBv1.0/edge_gateway/+/gw001")
        self.running = True

        self._cmd_thread = threading.Thread(target=self._send_commands, daemon=True)
        self._cmd_thread.start()

    def _on_connect(self, client, userdata, flags, rc):
        print(f"[MQTT] Connected to {self.broker}:{self.port} (rc={rc})")

    def _on_message(self, client, userdata, msg):
        self.received += 1

    def _send_commands(self):
        commands = [
            '{"method":"get_device_health","id":"%d"}',
            '{"method":"get_temp","id":"%d"}',
            '{"method":"set_alarm_threshold","params":{"temp_max":%.1f},"id":"%d"}',
            '{"method":"start_analysis","params":{"camera":"zone_A"},"id":"%d"}',
            '{"method":"stop_analysis","params":{"camera":"zone_A"},"id":"%d"}',
        ]

        cmd_topic = "spBv1.0/edge_gateway/DCMD/gw001"

        interval = 1.0 / self.rate if self.rate > 0 else 1.0
        seq = 0

        while self.running:
            cmd_template = random.choice(commands)
            seq += 1

            if "temp_max" in cmd_template:
                payload = cmd_template % (random.uniform(60, 95), seq)
            else:
                payload = cmd_template % seq

            result = self.client.publish(cmd_topic, payload, qos=1)
            if result.rc == 0:
                self.sent += 1
            else:
                self.errors += 1

            time.sleep(interval)

    def stop(self):
        self.running = False
        if self.client:
            self.client.loop_stop()
            self.client.disconnect()

    def stats(self):
        return {
            "mqtt_sent": self.sent,
            "mqtt_received": self.received,
            "mqtt_errors": self.errors,
        }


# ── Modbus 压测 ──

class ModbusLoadTester:
    """模拟多设备高频率 Modbus 轮询"""

    def __init__(self, port="/dev/ttyUSB0", num_devices=5, interval_ms=100):
        self.port = port
        self.num_devices = num_devices
        self.interval_ms = interval_ms
        self.requests = 0
        self.errors = 0
        self.running = False

    def start(self):
        import minimalmodbus

        self.instruments = []
        for i in range(self.num_devices):
            instr = minimalmodbus.Instrument(self.port, slaveaddress=i + 1)
            instr.serial.baudrate = 9600
            instr.serial.timeout = 0.5
            self.instruments.append(instr)

        self.running = True
        self._thread = threading.Thread(target=self._poll_loop, daemon=True)
        self._thread.start()

    def _poll_loop(self):
        while self.running:
            for instr in self.instruments:
                try:
                    # 读保持寄存器
                    values = instr.read_registers(0, 4)
                    self.requests += 1
                except Exception:
                    self.errors += 1
            time.sleep(self.interval_ms / 1000.0)

    def stop(self):
        self.running = False

    def stats(self):
        return {
            "modbus_requests": self.requests,
            "modbus_errors": self.errors,
        }


# ── 主函数 ──

def main():
    parser = argparse.ArgumentParser(description="网关高并发压测工具")
    parser.add_argument("--mqtt", action="store_true", help="MQTT 压测")
    parser.add_argument("--modbus", action="store_true", help="Modbus 压测")
    parser.add_argument("--all", action="store_true", help="全压测")
    parser.add_argument("--duration", type=int, default=300, help="压测时长（秒）")
    parser.add_argument("--mqtt-rate", type=int, default=20, help="MQTT 每秒指令数")
    parser.add_argument("--modbus-devices", type=int, default=5, help="Modbus 设备数")
    parser.add_argument("--modbus-port", default="/dev/ttyUSB0", help="Modbus 串口")
    parser.add_argument("--mqtt-broker", default="localhost", help="MQTT Broker 地址")
    parser.add_argument("--mqtt-port", type=int, default=1883, help="MQTT Broker 端口")
    args = parser.parse_args()

    run_mqtt = args.mqtt or args.all
    run_modbus = args.modbus or args.all

    if not run_mqtt and not run_modbus:
        parser.print_help()
        return

    print("=" * 60)
    print(f"网关压测开始 — {datetime.now().strftime('%Y-%m-%d %H:%M:%S')}")
    print(f"压测时长: {args.duration}s")
    if run_mqtt:
        print(f"MQTT: broker={args.mqtt_broker}:{args.mqtt_port}, rate={args.mqtt_rate}/s")
    if run_modbus:
        print(f"Modbus: port={args.modbus_port}, devices={args.modbus_devices}")
    print("=" * 60)

    testers = []
    if run_mqtt:
        mqtt_tester = MqttLoadTester(args.mqtt_broker, args.mqtt_port, args.mqtt_rate)
        mqtt_tester.start()
        testers.append(("MQTT", mqtt_tester))

    if run_modbus:
        modbus_tester = ModbusLoadTester(args.modbus_port, args.modbus_devices)
        modbus_tester.start()
        testers.append(("Modbus", modbus_tester))

    # 进度报告
    start = time.time()
    last_report = start
    try:
        while time.time() - start < args.duration:
            time.sleep(5)
            elapsed = time.time() - start
            if time.time() - last_report >= 10:
                print(f"[{elapsed:.0f}s/{args.duration}s] ", end="")
                for name, tester in testers:
                    stats = tester.stats()
                    print(f"{name}: {stats} ", end="")
                print()
                last_report = time.time()
    except KeyboardInterrupt:
        print("\n用户中断")

    # 停止 & 汇总
    print("\n--- 压测结果 ---")
    for name, tester in testers:
        tester.stop()
        stats = tester.stats()
        print(f"{name}: {json.dumps(stats, indent=2)}")

    print(f"\n压测完成 — {datetime.now().strftime('%Y-%m-%d %H:%M:%S')}")


if __name__ == "__main__":
    main()