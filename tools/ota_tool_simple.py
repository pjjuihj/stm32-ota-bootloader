#!/usr/bin/env python3
"""
OTA Tool - 最简版本（复制测试脚本逻辑）
"""

import serial
import serial.tools.list_ports
import time
import binascii
import sys


def read_hex_file(path):
    data = {}
    base_addr = 0
    with open(path, 'r') as f:
        for line in f:
            line = line.strip()
            if not line.startswith(':'):
                continue
            byte_count = int(line[1:3], 16)
            address = int(line[3:7], 16)
            record_type = int(line[7:9], 16)
            if record_type == 0x00:
                for i in range(byte_count):
                    data[base_addr + address + i] = int(line[9 + i*2:11 + i*2], 16)
            elif record_type == 0x01:
                break
            elif record_type == 0x02:
                base_addr = int(line[9:13], 16) << 4
            elif record_type == 0x04:
                base_addr = int(line[9:13], 16) << 16
    if not data:
        return b'', 0
    min_addr = min(data.keys())
    max_addr = max(data.keys())
    result = bytearray(max_addr - min_addr + 1)
    for addr, val in data.items():
        result[addr - min_addr] = val
    return bytes(result), min_addr


def main():
    # 支持命令行参数
    if len(sys.argv) >= 4:
        PORT = sys.argv[1]
        BAUD = int(sys.argv[2])
        FIRMWARE_PATH = sys.argv[3]
    else:
        PORT = 'COM3'
        BAUD = 115200
        FIRMWARE_PATH = r'D:\stm32 _project_hal\test\MDK-ARM\test1\test1.hex'

    # 读取固件
    print('Reading firmware...')
    firmware_data, _ = read_hex_file(FIRMWARE_PATH)
    print(f'Firmware: {len(firmware_data)} bytes')
    print()

    # 连接串口
    ser = serial.Serial(PORT, BAUD, timeout=2)
    time.sleep(1)
    ser.reset_input_buffer()

    def send_cmd(cmd, wait=1.0):
        ser.reset_input_buffer()
        ser.write((cmd + '\r\n').encode())
        time.sleep(wait)
        return ser.read(ser.in_waiting).decode('utf-8', errors='ignore').strip()

    # 测试连接
    print('Testing connection...')
    resp = send_cmd('version')
    print(f'  {resp}')
    print()

    if 'Bootloader' not in resp:
        print('ERROR: Cannot connect')
        ser.close()
        return

    # 进入 Boot
    print('Entering boot mode...')
    send_cmd('ota_enter', 1)
    time.sleep(2)
    ser.reset_input_buffer()

    # 开始 OTA
    print('Starting OTA...')
    resp = send_cmd(f'ota_start {len(firmware_data)}', 2)
    print(f'  {resp}')
    print()

    # 发送固件
    print('Sending firmware...')
    start_time = time.time()
    chunk_size = 128
    sent = 0
    for i in range(0, len(firmware_data), chunk_size):
        chunk = firmware_data[i:i+chunk_size]
        hex_data = chunk.hex()
        ser.write(f'ota_data {hex_data}\r\n'.encode())
        time.sleep(0.2)
        sent += len(chunk)
        progress = sent * 100 // len(firmware_data)
        print(f'\r  Progress: {progress}%', end='', flush=True)
    elapsed = time.time() - start_time
    print()
    print(f'  Time: {elapsed:.1f}s')
    print()

    # 结束 OTA
    print('Verifying...')
    crc = binascii.crc32(firmware_data) & 0xFFFFFFFF
    resp = send_cmd(f'ota_end {crc:08X}', 3)
    for line in resp.split('\n'):
        if line.strip():
            print(f'  {line.strip()}')
    print()

    if 'match=YES' in resp:
        print('=== OTA SUCCESS! ===')
    else:
        print('=== OTA FAILED ===')

    ser.close()


if __name__ == '__main__':
    main()
