#!/usr/bin/env python3
"""
STM32 Bootloader 串口自动测试脚本

使用方法:
    python test_bootloader_serial.py [COM端口]

例如:
    python test_bootloader_serial.py COM3
"""

import sys
import serial
import time

# 配置
BAUDRATE = 115200
TIMEOUT = 2  # 秒

class BootloaderTester:
    def __init__(self, port):
        self.port = port
        self.ser = None
        self.passed = 0
        self.failed = 0

    def connect(self):
        """连接串口"""
        try:
            self.ser = serial.Serial(
                port=self.port,
                baudrate=BAUDRATE,
                timeout=TIMEOUT
            )
            # 等待连接稳定
            time.sleep(0.5)
            # 清空缓冲区
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
            print("已断开连接")

    def send_command(self, cmd, wait=0.5):
        """发送命令并等待响应"""
        # 清空接收缓冲区
        self.ser.reset_input_buffer()

        # 发送命令
        self.ser.write((cmd + "\n").encode())
        time.sleep(wait)

        # 读取响应
        response = []
        while self.ser.in_waiting:
            line = self.ser.readline().decode('utf-8', errors='ignore').strip()
            if line:
                response.append(line)

        return response

    def test_command(self, name, cmd, expected=None, must_contain=None):
        """测试单个命令"""
        print(f"\n测试: {name}")
        print(f"  发送: {cmd}")

        response = self.send_command(cmd)

        if not response:
            print(f"  ❌ 无响应")
            self.failed += 1
            return False

        print(f"  收到:")
        for line in response:
            print(f"    {line}")

        # 检查期望的响应
        if expected:
            if any(expected in line for line in response):
                print(f"  ✅ 通过")
                self.passed += 1
                return True
            else:
                print(f"  ❌ 失败 - 未找到: {expected}")
                self.failed += 1
                return False

        if must_contain:
            if any(must_contain in line for line in response):
                print(f"  ✅ 通过")
                self.passed += 1
                return True
            else:
                print(f"  ❌ 失败 - 未找到: {must_contain}")
                self.failed += 1
                return False

        # 只要有响应就算通过
        print(f"  ✅ 通过")
        self.passed += 1
        return True

    def run_all_tests(self):
        """运行所有测试"""
        print("=" * 60)
        print("STM32 Bootloader 串口自动测试")
        print("=" * 60)

        if not self.connect():
            return

        try:
            # 测试1: 版本信息
            self.test_command(
                "版本信息",
                "version",
                must_contain="Bootloader v"
            )

            # 测试2: 状态信息
            self.test_command(
                "状态信息",
                "status",
                must_contain="State:"
            )

            # 测试3: 分区信息
            self.test_command(
                "分区信息",
                "info",
                must_contain="Active:"
            )

            # 测试4: 运行单元测试
            self.test_command(
                "单元测试",
                "test",
                must_contain="Running tests"
            )

            # 测试5: 进入Boot模式
            self.test_command(
                "进入Boot模式",
                "ota_enter",
                must_contain="Boot mode set"
            )

            # 测试6: 版本信息再次验证
            self.test_command(
                "版本信息 (再次)",
                "version",
                must_contain="Bootloader v"
            )

        finally:
            self.disconnect()

        # 打印结果
        print("\n" + "=" * 60)
        print("测试结果")
        print("=" * 60)
        print(f"通过: {self.passed}")
        print(f"失败: {self.failed}")
        print(f"总计: {self.passed + self.failed}")
        print("=" * 60)

        return self.failed == 0

def main():
    # 获取端口
    if len(sys.argv) > 1:
        port = sys.argv[1]
    else:
        port = "COM3"  # 默认端口
        print(f"使用默认端口: {port}")
        print(f"用法: python {sys.argv[0]} [COM端口]")

    # 运行测试
    tester = BootloaderTester(port)
    success = tester.run_all_tests()

    # 返回结果
    sys.exit(0 if success else 1)

if __name__ == "__main__":
    main()
