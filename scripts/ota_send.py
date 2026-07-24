#!/usr/bin/env python3
"""
OTA 固件更新脚本 (A/B 分区版本)
用法: python scripts/ota_send.py <hex文件路径>
"""

import serial
import time
import sys
import os
import binascii

def send_command(ser, cmd, wait_time=0.5):
    """发送命令并等待响应"""
    ser.write((cmd + '\r\n').encode())
    time.sleep(wait_time)
    response = ser.read(ser.in_waiting).decode('utf-8', errors='ignore')
    return response.strip()

def wait_for_all_responses(ser, timeout=5):
    """等待所有响应完成"""
    start = time.time()
    all_data = ""
    while time.time() - start < timeout:
        if ser.in_waiting:
            data = ser.read(ser.in_waiting).decode('utf-8', errors='ignore')
            all_data += data
            time.sleep(0.1)  # 等待更多数据
        else:
            time.sleep(0.05)
    return all_data

def wait_for_response(ser, keyword, timeout=5):
    """等待包含关键字的响应"""
    start = time.time()
    response = ""
    while time.time() - start < timeout:
        if ser.in_waiting:
            data = ser.read(ser.in_waiting).decode('utf-8', errors='ignore')
            response += data
            if keyword in response:
                return response
        time.sleep(0.05)
    return response

def read_hex_file(hex_file):
    """读取 HEX 文件并转换为二进制数据 (去除尾部 0xFF)"""
    records = {}

    with open(hex_file, 'r') as f:
        for line in f:
            line = line.strip()
            if not line.startswith(':'):
                continue

            byte_count = int(line[1:3], 16)
            address = int(line[3:7], 16)
            record_type = int(line[7:9], 16)

            if record_type == 0x00:  # Data record
                data_hex = line[9:9 + byte_count * 2]
                data = bytes.fromhex(data_hex)

                for i, byte in enumerate(data):
                    records[address + i] = byte

    if not records:
        return b'', 0

    min_addr = min(records.keys())
    max_addr = max(records.keys()) + 1

    # 构建二进制数据
    binary_data = bytearray()
    for addr in range(min_addr, max_addr):
        binary_data.append(records.get(addr, 0xFF))

    # 去除尾部 0xFF
    while binary_data and binary_data[-1] == 0xFF:
        binary_data.pop()

    return bytes(binary_data), min_addr

def calculate_crc32(data):
    """计算 CRC32"""
    return binascii.crc32(data) & 0xFFFFFFFF

def main():
    if len(sys.argv) < 2:
        print("用法: python scripts/ota_send.py <hex文件路径>")
        sys.exit(1)

    hex_file = sys.argv[1]

    if not os.path.exists(hex_file):
        print(f"错误: 文件不存在 - {hex_file}")
        sys.exit(1)

    print(f"固件文件: {hex_file}")

    # 读取 HEX 文件
    print("读取 HEX 文件...")
    firmware_data, base_addr = read_hex_file(hex_file)
    firmware_size = len(firmware_data)
    firmware_crc = calculate_crc32(firmware_data)
    print(f"基地址: 0x{base_addr:08X}")
    print(f"固件大小: {firmware_size} bytes")
    print(f"CRC32: 0x{firmware_crc:08X}")

    # 注意：Bootloader 期望从偏移 0 开始写入
    # hex 文件中的基地址 (0x0800C000) 只是参考，实际写入从 0 开始

    # 连接串口
    try:
        ser = serial.Serial('COM3', 115200, timeout=2)
        print("串口连接成功")
    except Exception as e:
        print(f"串口连接失败: {e}")
        sys.exit(1)

    try:
        ser.reset_input_buffer()

        # 检查状态
        print("\n=== 检查状态 ===")
        response = send_command(ser, 'info')
        print(response)

        # 开始 OTA
        print("\n=== 开始 OTA ===")
        ser.write(f'ota_start {firmware_size}\r\n'.encode())
        # 等待所有响应完成
        response = wait_for_all_responses(ser, timeout=5)
        print(response)
        if 'OK' not in response:
            print("开始 OTA 失败")
            sys.exit(1)

        # 分块发送固件数据
        print("\n=== 发送固件数据 ===")
        chunk_size = 128  # 保持 128 字节
        sent_bytes = 0
        start_time = time.time()

        for i in range(0, firmware_size, chunk_size):
            chunk = firmware_data[i:i + chunk_size]
            hex_data = chunk.hex()

            ser.write(f'ota_data {hex_data}\r\n'.encode())
            # 等待 Flash 写入完成 (~70ms) + 余量
            time.sleep(0.1)
            # 读取响应
            while ser.in_waiting:
                ser.read(ser.in_waiting)

            sent_bytes += len(chunk)
            progress = sent_bytes * 100 // firmware_size
            elapsed = time.time() - start_time
            speed = sent_bytes / elapsed if elapsed > 0 else 0

            sys.stdout.write(f"\rProgress: {progress}% ({sent_bytes}/{firmware_size}) Speed: {speed:.0f} B/s")
            sys.stdout.flush()

        print("\n")

        # 结束 OTA
        print("=== 结束 OTA ===")
        response = send_command(ser, f'ota_end {firmware_crc:08x}', wait_time=5)
        print(response)

        if 'OK' in response:
            print("\n=== OTA 更新成功! ===")
        else:
            print("\n=== OTA 更新失败! ===")

    except Exception as e:
        print(f"错误: {e}")
    finally:
        ser.close()

if __name__ == "__main__":
    main()
