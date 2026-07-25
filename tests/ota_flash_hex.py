#!/usr/bin/env python3
"""
STM32 Bootloader OTA 烧录脚本 (支持 .hex 文件)

使用方法:
    python ota_flash_hex.py [COM端口] [固件文件]

例如:
    python ota_flash_hex.py COM3 firmware.hex
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

class IntelHexParser:
    """简单的 Intel HEX 文件解析器"""

    @staticmethod
    def load(filepath):
        """加载 Intel HEX 文件并返回二进制数据"""
        binary_data = {}
        base_address = 0

        with open(filepath, 'r') as f:
            for line in f:
                line = line.strip()
                if not line.startswith(':'):
                    continue

                # 解析 HEX 记录
                byte_count = int(line[1:3], 16)
                address = int(line[3:7], 16)
                record_type = int(line[7:9], 16)
                data = line[9:9+byte_count*2]

                if record_type == 0x00:  # 数据记录
                    for i in range(0, len(data), 2):
                        byte = int(data[i:i+2], 16)
                        binary_data[base_address + address + i//2] = byte
                elif record_type == 0x01:  # 文件结束记录
                    break
                elif record_type == 0x02:  # 扩展段地址记录
                    base_address = int(data, 16) << 4
                elif record_type == 0x04:  # 扩展线性地址记录
                    base_address = int(data, 16) << 16

        if not binary_data:
            return None

        # 转换为字节数组
        min_addr = min(binary_data.keys())
        max_addr = max(binary_data.keys())
        result = bytearray(max_addr - min_addr + 1)

        for addr, byte in binary_data.items():
            result[addr - min_addr] = byte

        return bytes(result)

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
            print(f"[OK] 已连接到 {self.port}")
            return True
        except serial.SerialException as e:
            print(f"[FAIL] 连接失败: {e}")
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
            if self.firmware_path.endswith('.hex'):
                print(f"  解析 Intel HEX 文件...")
                firmware = IntelHexParser.load(self.firmware_path)
                if firmware is None:
                    print(f"[FAIL] 解析 HEX 文件失败")
                    return False
            else:
                with open(self.firmware_path, 'rb') as f:
                    firmware = f.read()
            print(f"[OK] 固件文件: {self.firmware_path}")
            print(f"   大小: {len(firmware)} 字节")
        except FileNotFoundError:
            print(f"[FAIL] 找不到固件文件: {self.firmware_path}")
            return False
        except Exception as e:
            print(f"[FAIL] 读取固件失败: {e}")
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
                print("[FAIL] 进入 Boot 模式失败")
                return False

            # 等待设备复位
            print("\n  等待设备复位...")
            time.sleep(3)

            # 重新连接
            self.ser.reset_input_buffer()

            # 步骤2: 开始 OTA
            print(f"\n[2/5] 开始 OTA (大小: {len(firmware)} 字节)...")
            response = self.send_command(f"ota_start {len(firmware)}", wait=5)
            print(f"  收到: {response}")
            if not any("OK:Start" in r for r in response):
                print("[FAIL] 开始 OTA 失败")
                return False

            # 步骤3: 发送固件数据
            print(f"\n[3/5] 发送固件数据...")
            total_chunks = (len(firmware) + CHUNK_SIZE - 1) // CHUNK_SIZE
            for i in range(0, len(firmware), CHUNK_SIZE):
                chunk = firmware[i:i+CHUNK_SIZE]
                hex_data = chunk.hex()

                response = self.send_command(f"ota_data {hex_data}", wait=0.1)

                # 显示进度
                progress = (i + len(chunk)) * 100 // len(firmware)
                print(f"\r  进度: {progress}% ({i + len(chunk)}/{len(firmware)})", end="", flush=True)

                # 检查响应
                if any("ERROR" in r for r in response):
                    print(f"\n[FAIL] 发送失败: {response}")
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
                print("[OK] OTA 烧录成功！")
                print("=" * 60)
                print("设备将在3秒后自动复位...")
                return True
            else:
                print("\n" + "=" * 60)
                print("[FAIL] OTA 烧录失败")
                print("=" * 60)
                return False

        finally:
            self.disconnect()

def main():
    # 获取参数
    if len(sys.argv) < 3:
        print(f"用法: python {sys.argv[0]} [COM端口] [固件文件]")
        print(f"例如: python {sys.argv[0]} COM3 firmware.hex")
        sys.exit(1)

    port = sys.argv[1]
    firmware_path = sys.argv[2]

    # 执行烧录
    flasher = OTAFlasher(port, firmware_path)
    success = flasher.flash()

    sys.exit(0 if success else 1)

if __name__ == "__main__":
    main()
