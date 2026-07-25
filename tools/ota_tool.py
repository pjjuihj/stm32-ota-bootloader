#!/usr/bin/env python3
"""
STM32 OTA Bootloader 上位机工具 v1.0
通过串口与 STM32 Bootloader 通信，进行固件升级

使用方法:
    python ota_tool.py                    # 启动图形界面
    python ota_tool.py --port COM3 --file firmware.hex  # 命令行模式
"""

import serial
import serial.tools.list_ports
import time
import binascii
import sys
import os
import threading
import tkinter as tk
from tkinter import ttk, filedialog, messagebox, scrolledtext


# ============================================================
# 配置
# ============================================================

DEFAULT_BAUD = 115200
CHUNK_SIZE = 128


# ============================================================
# HEX 文件解析
# ============================================================

def read_hex_file(path):
    """读取 Intel HEX 文件"""
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


# ============================================================
# Bootloader 通信协议
# ============================================================

class BootloaderProtocol:
    def __init__(self, port, baud=DEFAULT_BAUD):
        self.port = port
        self.baud = baud
        self.ser = None
        self.connected = False

    def connect(self):
        try:
            self.ser = serial.Serial(self.port, self.baud, timeout=2)
            time.sleep(0.5)
            self.ser.reset_input_buffer()
            self.connected = True
            return True
        except Exception as e:
            print(f"连接失败: {e}")
            return False

    def disconnect(self):
        if self.ser and self.ser.is_open:
            self.ser.close()
        self.connected = False

    def send_command(self, cmd, wait=1.0):
        if not self.connected:
            return ""
        self.ser.reset_input_buffer()
        self.ser.write((cmd + '\r\n').encode())
        time.sleep(wait)
        return self.ser.read(self.ser.in_waiting).decode('utf-8', errors='ignore').strip()

    def get_version(self):
        return self.send_command('version')

    def get_status(self):
        return self.send_command('ota_status')

    def get_info(self):
        return self.send_command('info')

    def enter_boot_mode(self):
        return self.send_command('ota_enter')

    def start_ota(self, size):
        return self.send_command(f'ota_start {size}', wait=2.0)

    def send_firmware_data(self, hex_data):
        return self.send_command(f'ota_data {hex_data}', wait=0.3)

    def end_ota(self, crc):
        return self.send_command(f'ota_end {crc:08X}', wait=3.0)

    def reset_device(self):
        return self.send_command('reset')

    def led_on(self):
        return self.send_command('led_on')

    def led_off(self):
        return self.send_command('led_off')


# ============================================================
# OTA 升级器
# ============================================================

class OTAUpdater:
    def __init__(self, protocol, callback=None):
        self.protocol = protocol
        self.callback = callback
        self.running = False
        self.success = False

    def log(self, msg):
        if self.callback:
            self.callback('log', msg)

    def update_progress(self, progress, detail=""):
        if self.callback:
            self.callback('progress', progress, detail)

    def ota_upgrade(self, hex_path):
        self.running = True
        self.success = False
        try:
            self.log(f"[1] 读取固件: {hex_path}")
            firmware_data, start_addr = read_hex_file(hex_path)
            if not firmware_data:
                self.log("  错误: 无法读取固件文件")
                return False
            size = len(firmware_data)
            crc = binascii.crc32(firmware_data) & 0xFFFFFFFF
            self.log(f"  大小: {size} 字节 | 地址: 0x{start_addr:08X} | CRC: 0x{crc:08X}")

            self.log("[2] 检查连接...")
            version = self.protocol.get_version()
            if not version:
                self.log("  错误: 设备无响应")
                return False
            self.log(f"  {version[:60]}")

            self.log("[3] 进入 Boot 模式...")
            resp = self.protocol.enter_boot_mode()
            self.log(f"  {resp}")
            time.sleep(2)
            self.protocol.ser.reset_input_buffer()

            self.log("[4] 确认 Bootloader...")
            version = self.protocol.get_version()
            if 'Bootloader' not in version:
                self.log("  错误: 未进入 Bootloader")
                return False
            self.log(f"  {version[:60]}")

            self.log("[5] 开始 OTA...")
            resp = self.protocol.start_ota(size)
            self.log(f"  {resp}")

            self.log("[6] 发送固件...")
            sent = 0
            for i in range(0, size, CHUNK_SIZE):
                if not self.running:
                    self.log("  用户取消")
                    return False
                chunk = firmware_data[i:i+CHUNK_SIZE]
                hex_data = chunk.hex()
                resp = self.protocol.send_firmware_data(hex_data)
                sent += len(chunk)
                progress = sent * 100 // size
                self.update_progress(progress, f"{sent}/{size}")
                if 'ERROR' in resp:
                    self.log(f"  错误: {resp}")
                    return False
            self.log(f"  发送完成: {sent}/{size}")

            self.log("[7] 校验固件...")
            resp = self.protocol.end_ota(crc)
            for line in resp.split('\n'):
                if line.strip():
                    self.log(f"  {line.strip()}")

            if 'match=YES' in resp or 'OK' in resp:
                self.log("=== OTA 升级成功！ ===")
                self.success = True
                return True
            else:
                self.log("=== OTA 升级失败！ ===")
                return False
        except Exception as e:
            self.log(f"错误: {e}")
            return False
        finally:
            self.running = False

    def cancel(self):
        self.running = False


# ============================================================
# 图形界面
# ============================================================

class OTAToolGUI:
    def __init__(self):
        self.root = tk.Tk()
        self.root.title("STM32 OTA Bootloader Tool v1.0")
        self.root.geometry("750x650")
        self.root.minsize(600, 500)

        self.protocol = None
        self.updater = None
        self.connected = False

        # 颜色配置
        self.colors = {
            'bg': '#f5f5f5',
            'card': '#ffffff',
            'accent': '#2196F3',
            'success': '#4CAF50',
            'error': '#f44336',
            'text': '#333333',
            'text_light': '#666666',
            'log_bg': '#1e1e1e',
            'log_fg': '#d4d4d4',
        }

        self.root.configure(bg=self.colors['bg'])
        self.create_widgets()

    def create_widgets(self):
        main_frame = ttk.Frame(self.root, padding=15)
        main_frame.pack(fill=tk.BOTH, expand=True)

        # ===== 串口连接区 =====
        conn_frame = ttk.LabelFrame(main_frame, text=" 串口连接 ", padding=12)
        conn_frame.pack(fill=tk.X, pady=(0, 10))

        row1 = ttk.Frame(conn_frame)
        row1.pack(fill=tk.X)

        ttk.Label(row1, text="串口:").pack(side=tk.LEFT, padx=(0, 5))
        self.port_combo = ttk.Combobox(row1, width=15, state="readonly")
        self.port_combo.pack(side=tk.LEFT, padx=(0, 8))
        self.refresh_ports()

        ttk.Button(row1, text="刷新", command=self.refresh_ports, width=6).pack(side=tk.LEFT, padx=(0, 15))

        ttk.Label(row1, text="波特率:").pack(side=tk.LEFT, padx=(0, 5))
        self.baud_combo = ttk.Combobox(row1, width=8, values=["9600", "57600", "115200", "230400", "460800"])
        self.baud_combo.set("115200")
        self.baud_combo.pack(side=tk.LEFT, padx=(0, 15))

        self.connect_btn = ttk.Button(row1, text="连接", command=self.toggle_connect, width=10)
        self.connect_btn.pack(side=tk.LEFT, padx=(0, 10))

        self.status_canvas = tk.Canvas(row1, width=18, height=18, highlightthickness=0, bg=self.colors['bg'])
        self.status_canvas.pack(side=tk.LEFT, padx=(0, 5))
        self.status_dot = self.status_canvas.create_oval(3, 3, 15, 15, fill="gray", outline="gray")

        self.status_label = ttk.Label(row1, text="未连接", foreground="gray")
        self.status_label.pack(side=tk.LEFT)

        # ===== 固件选择区 =====
        fw_frame = ttk.LabelFrame(main_frame, text=" 固件选择 ", padding=12)
        fw_frame.pack(fill=tk.X, pady=(0, 10))

        file_row = ttk.Frame(fw_frame)
        file_row.pack(fill=tk.X, pady=(0, 8))

        self.fw_path_var = tk.StringVar()
        self.fw_entry = ttk.Entry(file_row, textvariable=self.fw_path_var, width=55)
        self.fw_entry.pack(side=tk.LEFT, fill=tk.X, expand=True, padx=(0, 8))

        ttk.Button(file_row, text="浏览...", command=self.browse_file, width=10).pack(side=tk.LEFT)

        self.fw_info_label = ttk.Label(fw_frame, text="未选择文件", foreground="gray")
        self.fw_info_label.pack(anchor=tk.W)

        # ===== 操作按钮区 =====
        btn_frame = ttk.Frame(main_frame)
        btn_frame.pack(fill=tk.X, pady=(0, 10))

        self.ota_btn = ttk.Button(btn_frame, text="开始升级", command=self.start_ota, width=14)
        self.ota_btn.pack(side=tk.LEFT, padx=(0, 5))

        self.cancel_btn = ttk.Button(btn_frame, text="取消", command=self.cancel_ota, state=tk.DISABLED, width=8)
        self.cancel_btn.pack(side=tk.LEFT, padx=(0, 20))

        ttk.Separator(btn_frame, orient=tk.VERTICAL).pack(side=tk.LEFT, fill=tk.Y, padx=8)

        ttk.Button(btn_frame, text="版本", command=self.get_version, width=7).pack(side=tk.LEFT, padx=3)
        ttk.Button(btn_frame, text="状态", command=self.get_status, width=7).pack(side=tk.LEFT, padx=3)
        ttk.Button(btn_frame, text="LED", command=self.test_led, width=7).pack(side=tk.LEFT, padx=3)
        ttk.Button(btn_frame, text="信息", command=self.get_info, width=7).pack(side=tk.LEFT, padx=3)

        # ===== 进度区 =====
        progress_frame = ttk.Frame(main_frame)
        progress_frame.pack(fill=tk.X, pady=(0, 8))

        self.progress_var = tk.DoubleVar()
        self.progress_bar = ttk.Progressbar(progress_frame, variable=self.progress_var, maximum=100)
        self.progress_bar.pack(side=tk.LEFT, fill=tk.X, expand=True, padx=(0, 10))

        self.progress_percent = ttk.Label(progress_frame, text="0%", width=6, font=('Arial', 11, 'bold'))
        self.progress_percent.pack(side=tk.LEFT)

        self.progress_label = ttk.Label(main_frame, text="就绪", foreground="gray")
        self.progress_label.pack(anchor=tk.W, pady=(0, 5))

        # ===== 日志区 =====
        log_frame = ttk.LabelFrame(main_frame, text=" 通信日志 ", padding=8)
        log_frame.pack(fill=tk.BOTH, expand=True)

        log_toolbar = ttk.Frame(log_frame)
        log_toolbar.pack(fill=tk.X, pady=(0, 5))

        ttk.Button(log_toolbar, text="清除日志", command=self.clear_log, width=10).pack(side=tk.LEFT)
        ttk.Button(log_toolbar, text="保存日志", command=self.save_log, width=10).pack(side=tk.LEFT, padx=(5, 0))

        self.log_text = scrolledtext.ScrolledText(log_frame, height=10, state=tk.DISABLED,
                                                   font=('Consolas', 9), bg=self.colors['log_bg'],
                                                   fg=self.colors['log_fg'], insertbackground='white',
                                                   relief=tk.FLAT, bd=2)
        self.log_text.pack(fill=tk.BOTH, expand=True)

        # 日志颜色标签
        self.log_text.tag_config('info', foreground='#4ec9b0')
        self.log_text.tag_config('success', foreground='#6a9955')
        self.log_text.tag_config('error', foreground='#f44747')
        self.log_text.tag_config('warning', foreground='#ce9178')

    def refresh_ports(self):
        ports = serial.tools.list_ports.comports()
        port_list = [p.device for p in ports]
        self.port_combo['values'] = port_list
        if port_list:
            self.port_combo.set(port_list[0])

    def toggle_connect(self):
        if self.connected:
            self.disconnect()
        else:
            self.connect()

    def connect(self):
        port_text = self.port_combo.get()
        if not port_text:
            self.log("请选择串口", 'error')
            return
        port = port_text.split(" - ")[0] if " - " in port_text else port_text
        baud = int(self.baud_combo.get())

        self.protocol = BootloaderProtocol(port, baud)
        if self.protocol.connect():
            self.connected = True
            self.connect_btn.config(text="断开")
            self.status_canvas.itemconfig(self.status_dot, fill="green")
            self.status_label.config(text="已连接", foreground="green")
            self.log(f"已连接: {port} @ {baud}", 'success')
            version = self.protocol.get_version()
            if version:
                self.log(f"设备: {version}", 'info')
        else:
            self.log("连接失败", 'error')

    def disconnect(self):
        if self.protocol:
            self.protocol.disconnect()
        self.connected = False
        self.connect_btn.config(text="连接")
        self.status_canvas.itemconfig(self.status_dot, fill="gray")
        self.status_label.config(text="未连接", foreground="gray")
        self.log("已断开连接", 'info')

    def browse_file(self):
        path = filedialog.askopenfilename(
            title="选择固件文件",
            filetypes=[("HEX 文件", "*.hex"), ("所有文件", "*.*")]
        )
        if path:
            self.fw_path_var.set(path)
            try:
                data, addr = read_hex_file(path)
                crc = binascii.crc32(data) & 0xFFFFFFFF
                self.fw_info_label.config(
                    text=f"大小: {len(data)} 字节 | 地址: 0x{addr:08X} | CRC: 0x{crc:08X}",
                    foreground="green"
                )
                self.log(f"已加载固件: {os.path.basename(path)} ({len(data)} 字节)", 'info')
            except Exception as e:
                self.fw_info_label.config(text=f"错误: {e}", foreground="red")
                self.log(f"无法解析固件: {e}", 'error')

    def start_ota(self):
        if not self.connected:
            self.log("请先连接串口", 'error')
            return
        fw_path = self.fw_path_var.get()
        if not fw_path or not os.path.exists(fw_path):
            self.log("请选择有效的固件文件", 'error')
            return

        self.ota_btn.config(state=tk.DISABLED)
        self.cancel_btn.config(state=tk.NORMAL)
        self.progress_var.set(0)
        self.progress_percent.config(text="0%")
        self.progress_label.config(text="升级中...", foreground="blue")

        def progress_callback(event, *args):
            def update():
                if event == 'progress':
                    self.progress_var.set(args[0])
                    self.progress_percent.config(text=f"{args[0]}%")
                elif event == 'log':
                    self.log(args[0])
            self.root.after(0, update)

        self.updater = OTAUpdater(self.protocol, progress_callback)

        def run_ota():
            success = self.updater.ota_upgrade(fw_path)
            self.root.after(0, self.ota_complete, success)

        thread = threading.Thread(target=run_ota, daemon=True)
        thread.start()

    def ota_complete(self, success):
        self.ota_btn.config(state=tk.NORMAL)
        self.cancel_btn.config(state=tk.DISABLED)
        if success:
            self.progress_var.set(100)
            self.progress_percent.config(text="100%")
            self.progress_label.config(text="升级成功！", foreground="green")
            self.log("OTA 升级成功！", 'success')
        else:
            self.progress_label.config(text="升级失败", foreground="red")
            self.log("OTA 升级失败", 'error')

    def cancel_ota(self):
        if self.updater:
            self.updater.cancel()
        self.ota_btn.config(state=tk.NORMAL)
        self.cancel_btn.config(state=tk.DISABLED)
        self.progress_label.config(text="已取消", foreground="gray")
        self.log("用户取消升级", 'warning')

    def get_version(self):
        if not self.connected:
            self.log("请先连接串口", 'error')
            return
        resp = self.protocol.get_version()
        self.log(f"版本: {resp}", 'info')

    def get_status(self):
        if not self.connected:
            self.log("请先连接串口", 'error')
            return
        resp = self.protocol.get_status()
        self.log(f"状态: {resp}", 'info')

    def get_info(self):
        if not self.connected:
            self.log("请先连接串口", 'error')
            return
        resp = self.protocol.get_info()
        self.log(f"信息: {resp}", 'info')

    def test_led(self):
        if not self.connected:
            self.log("请先连接串口", 'error')
            return
        resp = self.protocol.led_on()
        self.log(f"LED ON: {resp}", 'info')
        time.sleep(1)
        resp = self.protocol.led_off()
        self.log(f"LED OFF: {resp}", 'info')

    def log(self, msg, tag='info'):
        self.log_text.config(state=tk.NORMAL)
        timestamp = time.strftime("%H:%M:%S")
        self.log_text.insert(tk.END, f"[{timestamp}] {msg}\n", tag)
        self.log_text.see(tk.END)
        self.log_text.config(state=tk.DISABLED)

    def clear_log(self):
        self.log_text.config(state=tk.NORMAL)
        self.log_text.delete(1.0, tk.END)
        self.log_text.config(state=tk.DISABLED)

    def save_log(self):
        path = filedialog.asksaveasfilename(
            title="保存日志", defaultextension=".log",
            filetypes=[("日志文件", "*.log"), ("所有文件", "*.*")]
        )
        if path:
            with open(path, 'w', encoding='utf-8') as f:
                f.write(self.log_text.get(1.0, tk.END))
            self.log(f"日志已保存: {path}", 'info')

    def run(self):
        self.root.mainloop()


# ============================================================
# 命令行模式
# ============================================================

def cli_mode(port, hex_path, baud=DEFAULT_BAUD):
    print("=" * 50)
    print("  OTA Bootloader Tool (CLI)")
    print("=" * 50)
    print(f"  Port: {port}")
    print(f"  File: {hex_path}")
    print(f"  Baud: {baud}")
    print("=" * 50)
    print()

    protocol = BootloaderProtocol(port, baud)
    if not protocol.connect():
        print("连接失败")
        return False

    try:
        def progress_callback(event, *args):
            if event == 'progress':
                print(f"\r  进度: {args[0]}%", end='', flush=True)
            elif event == 'log':
                print(args[0])
        updater = OTAUpdater(protocol, progress_callback)
        success = updater.ota_upgrade(hex_path)
        print()
        return success
    finally:
        protocol.disconnect()


# ============================================================
# 主函数
# ============================================================

def main():
    if len(sys.argv) > 1:
        import argparse
        parser = argparse.ArgumentParser(description="OTA Bootloader Tool")
        parser.add_argument("--port", help="串口名称")
        parser.add_argument("--file", help="固件文件路径")
        parser.add_argument("--baud", type=int, default=DEFAULT_BAUD, help="波特率")
        parser.add_argument("--gui", action="store_true", help="启动图形界面")
        args = parser.parse_args()

        if args.gui or (not args.port and not args.file):
            app = OTAToolGUI()
            app.run()
        elif args.port and args.file:
            cli_mode(args.port, args.file, args.baud)
        else:
            parser.print_help()
    else:
        app = OTAToolGUI()
        app.run()


if __name__ == '__main__':
    main()
