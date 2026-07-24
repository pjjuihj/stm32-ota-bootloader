#!/usr/bin/env python3
"""
STM32 Bootloader OTA 烧录脚本

使用方法:
    python ota_flash.py [COM端口] [固件文件]

例如:
    python ota_flash.py COM3 firmware.bin
"""

import sys
import serial
import time
import struct
import binascii

# 配置
BAUDRATE = 115200
TIMEOUT = 2
CHUNK_SIZE = 128  # 每次发送128字节

class OTAFlasher:
    def __init__(self, port, firmware_path):
        self.port = port
        self.firmware_path = firmware_path
        self.ser = None

    def connect(self):
        """连接串口"""
        try:
            self.ser = serial.Serial(
                port=self.port,
                baudrate=BAUDRATE,
                timeout=TIMEOUT
            )
            time.sleep(0.5)
            self.ser.reset_input_buffer()
            print(f"✅ 已连接到 {self.port}")
            return True
        except serial.SerialException as e:
            print(f"❌ 连接失败: {e}")
            return False

    def disconnect(self):
        """断开串口"""
        if self.ser and self.ser.is_open:
            self.ser.close()

    def send_command(self, cmd, wait=0.5):
        """发送命令并等待响应"""
        self.ser.reset_input_buffer()
        self.ser.write((cmd + "\n").encode())
        time.sleep(wait)

        response = []
        while self.ser.in_waiting:
            line = self.ser.readline().decode('utf-8', errors='ignore').strip()
            if line:
                response.append(line)
        return response

    def calculate_crc32(self, data):
        """计算 CRC32"""
        return binascii.crc32(data) & 0xFFFFFFFF

    def flash(self):
        """执行 OTA 烧录"""
        print("=" * 60)
        print("STM32 Bootloader OTA 烧录")
        print("=" * 60)

        # 读取固件文件
        try:
            with open(self.firmware_path, 'rb') as f:
                firmware = f.read()
            print(f"✅ 固件文件: {self.firmware_path}")
            print(f"   大小: {len(firmware)} 字节")
        except FileNotFoundError:
            print(f"❌ 找不到固件文件: {self.firmware_path}")
            return False

        # 连接串口
        if not self.connect():
            return False

        try:
            # 步骤1: 进入 Boot 模式
            print("\n[1/5] 进入 Boot 模式...")
            response = self.send_command("ota_enter")
            print(f"  收到: {response}")
            if not any("Boot mode set" in r for r in response):
                print("❌ 进入 Boot 模式失败")
                return False

            # 等待用户复位设备
            print("\n  请复位设备（按下复位按钮或断电重连）...")
            input("  按 Enter 继续...")

            # 重新连接
            time.sleep(1)
            self.ser.reset_input_buffer()

            # 步骤2: 开始 OTA
            print(f"\n[2/5] 开始 OTA (大小: {len(firmware)} 字节)...")
            response = self.send_command(f"ota_start {len(firmware)}")
            print(f"  收到: {response}")
            if not any("OK:Start" in r for r in response):
                print("❌ 开始 OTA 失败")
                return False

            # 步骤3: 发送固件数据
            print(f"\n[3/5] 发送固件数据...")
            total_chunks = (len(firmware) + CHUNK_SIZE - 1) // CHUNK_SIZE
            for i in range(0, len(firmware), CHUNK_SIZE):
                chunk = firmware[i:i+CHUNK_SIZE]
                hex_data = chunk.hex()
                chunk_num = i // CHUNK_SIZE + 1

                response = self.send_command(f"ota_data {hex_data}", wait=0.1)

                # 显示进度
                progress = (i + len(chunk)) * 100 // len(firmware)
                print(f"\r  进度: {progress}% ({i + len(chunk)}/{len(firmware)})", end="", flush=True)

                # 检查响应
                if any("ERROR" in r for r in response):
                    print(f"\n❌ 发送失败: {response}")
                    return False

            print()  # 换行

            # 步骤4: 计算并发送 CRC32
            print("\n[4/5] 计算 CRC32...")
            crc32 = self.calculate_crc32(firmware)
            print(f"  CRC32: 0x{crc32:08X}")

            print("\n[5/5] 验证固件...")
            response = self.send_command(f"ota_end {crc32:08X}")
            print(f"  收到: {response}")

            if any("OK:Verify passed" in r for r in response):
                print("\n" + "=" * 60)
                print("✅ OTA 烧录成功！")
                print("=" * 60)
                print("设备将在3秒后自动复位...")
                return True
            else:
                print("\n" + "=" * 60)
                print("❌ OTA 烧录失败")
                print("=" * 60)
                return False

        finally:
            self.disconnect()

def main():
    # 获取参数
    if len(sys.argv) < 3:
        print(f"用法: python {sys.argv[0]} [COM端口] [固件文件]")
        print(f"例如: python {sys.argv[0]} COM3 firmware.bin")
        sys.exit(1)

    port = sys.argv[1]
    firmware_path = sys.argv[2]

    # 执行烧录
    flasher = OTAFlasher(port, firmware_path)
    success = flasher.flash()

    sys.exit(0 if success else 1)

if __name__ == "__main__":
    main()
