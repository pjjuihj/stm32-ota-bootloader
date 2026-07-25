#!/usr/bin/env python3
"""
OTA Bootloader Tool - 使用标准 tkinter
"""

import tkinter as tk
from tkinter import ttk, filedialog, scrolledtext
import serial
import serial.tools.list_ports
import time
import binascii
import threading


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


class OTAApp:
    def __init__(self):
        self.root = tk.Tk()
        self.root.title("OTA Bootloader Tool")
        self.root.geometry("800x600")

        self.ser = None
        self.connected = False
        self.upgrading = False
        self.firmware_data = None

        self.create_ui()
        self.refresh_ports()

    def create_ui(self):
        # 左侧边栏
        sidebar = tk.Frame(self.root, width=200, bg="#f0f0f0")
        sidebar.pack(side="left", fill="y")
        sidebar.pack_propagate(False)

        tk.Label(sidebar, text="OTA Tool", font=("Arial", 14, "bold"), bg="#2196F3", fg="white").pack(fill="x", pady=10)

        # 串口配置
        tk.Label(sidebar, text="串口配置", font=("Arial", 10, "bold"), bg="#f0f0f0").pack(anchor="w", padx=10, pady=(10, 5))

        tk.Label(sidebar, text="端口号", bg="#f0f0f0").pack(anchor="w", padx=10)
        self.port_combo = ttk.Combobox(sidebar, values=[""], width=15, state="readonly")
        self.port_combo.pack(padx=10, pady=(0, 5))

        tk.Label(sidebar, text="波特率", bg="#f0f0f0").pack(anchor="w", padx=10)
        self.baud_combo = ttk.Combobox(sidebar, values=["9600", "57600", "115200", "230400"], width=15, state="readonly")
        self.baud_combo.pack(padx=10, pady=(0, 5))
        self.baud_combo.set("115200")

        self.connect_btn = tk.Button(sidebar, text="打开串口", command=self.toggle_connect, bg="#4CAF50", fg="white")
        self.connect_btn.pack(padx=10, pady=10, fill="x")

        self.status_label = tk.Label(sidebar, text="● 未连接", bg="#f0f0f0", fg="gray")
        self.status_label.pack(padx=10)

        # 右侧内容区
        right = tk.Frame(self.root, bg="#f0f0f0")
        right.pack(side="left", fill="both", expand=True)

        # 固件选择
        fw_frame = tk.LabelFrame(right, text="固件文件", bg="#ffffff")
        fw_frame.pack(fill="x", padx=10, pady=10)

        fw_inner = tk.Frame(fw_frame, bg="#ffffff")
        fw_inner.pack(fill="x", padx=10, pady=5)

        self.fw_entry = tk.Entry(fw_inner, width=50)
        self.fw_entry.pack(side="left", fill="x", expand=True, padx=(0, 5))

        tk.Button(fw_inner, text="浏览", command=self.browse_file).pack(side="left")

        self.fw_info = tk.Label(fw_frame, text="", bg="#ffffff", fg="gray")
        self.fw_info.pack(anchor="w", padx=10, pady=(0, 5))

        # 操作按钮
        btn_frame = tk.Frame(right, bg="#f0f0f0")
        btn_frame.pack(fill="x", padx=10, pady=5)

        self.upgrade_btn = tk.Button(btn_frame, text="开始升级", command=self.start_upgrade, bg="#FF5722", fg="white", width=12)
        self.upgrade_btn.pack(side="left", padx=5)

        self.cancel_btn = tk.Button(btn_frame, text="取消", command=self.cancel_upgrade, state="disabled", width=8)
        self.cancel_btn.pack(side="left", padx=5)

        tk.Button(btn_frame, text="版本", command=self.get_version, width=6).pack(side="left", padx=5)
        tk.Button(btn_frame, text="状态", command=self.get_status, width=6).pack(side="left", padx=5)
        tk.Button(btn_frame, text="LED", command=self.test_led, width=6).pack(side="left", padx=5)

        # 进度条
        self.progress = ttk.Progressbar(right, mode="determinate", length=400)
        self.progress.pack(padx=10, pady=5)

        self.progress_label = tk.Label(right, text="就绪", bg="#f0f0f0")
        self.progress_label.pack(padx=10)

        # 日志区
        log_frame = tk.LabelFrame(right, text="日志", bg="#ffffff")
        log_frame.pack(fill="both", expand=True, padx=10, pady=(0, 10))

        self.log_text = scrolledtext.ScrolledText(log_frame, height=10, font=("Consolas", 9))
        self.log_text.pack(fill="both", expand=True, padx=5, pady=5)

        tk.Button(log_frame, text="清除", command=self.clear_log).pack(pady=5)

        self.log("OTA Bootloader Tool 就绪")

    def refresh_ports(self):
        ports = serial.tools.list_ports.comports()
        port_list = [p.device for p in ports]
        self.port_combo['values'] = port_list if port_list else ["无串口"]
        if port_list:
            self.port_combo.set(port_list[0])
        self.log(f"检测到 {len(port_list)} 个串口")

    def toggle_connect(self):
        if self.connected:
            self.disconnect()
        else:
            self.connect()

    def connect(self):
        port = self.port_combo.get()
        if not port or port == "无串口":
            self.log("请选择串口")
            return
        try:
            baud = int(self.baud_combo.get())
            self.ser = serial.Serial(port, baud, timeout=2)
            time.sleep(0.5)
            self.ser.reset_input_buffer()
            self.connected = True
            self.connect_btn.config(text="关闭串口", bg="#F44336")
            self.status_label.config(text="● 已连接", fg="green")
            self.log(f"已连接 {port} @ {baud}")

            ver = self.send_cmd("version")
            if ver:
                self.log(f"设备: {ver}")
        except Exception as e:
            self.log(f"连接失败: {e}")

    def disconnect(self):
        if self.ser and self.ser.is_open:
            self.ser.close()
        self.connected = False
        self.ser = None
        self.connect_btn.config(text="打开串口", bg="#4CAF50")
        self.status_label.config(text="● 未连接", fg="gray")
        self.log("已关闭串口")

    def send_cmd(self, cmd, wait=1.0):
        if not self.ser or not self.connected:
            return ""
        try:
            self.ser.reset_input_buffer()
            self.ser.write((cmd + '\r\n').encode())
            time.sleep(wait)
            return self.ser.read(self.ser.in_waiting).decode('utf-8', errors='ignore').strip()
        except:
            return ""

    def browse_file(self):
        path = filedialog.askopenfilename(filetypes=[("HEX", "*.hex"), ("All", "*.*")])
        if path:
            self.fw_entry.delete(0, "end")
            self.fw_entry.insert(0, path)
            try:
                data, addr = read_hex_file(path)
                self.firmware_data = data
                crc = binascii.crc32(data) & 0xFFFFFFFF
                name = path.split("/")[-1].split("\\")[-1]
                self.fw_info.config(text=f"{name} | {len(data)} bytes | 0x{addr:08X} | 0x{crc:08X}")
                self.log(f"已加载: {name} ({len(data)} bytes)")
            except Exception as e:
                self.fw_info.config(text=f"错误: {e}")

    def start_upgrade(self):
        if not self.connected:
            self.log("请先连接串口")
            return
        if not self.firmware_data:
            self.log("请选择固件文件")
            return

        self.upgrading = True
        self.upgrade_btn.config(state="disabled")
        self.cancel_btn.config(state="normal")
        self.progress['value'] = 0
        self.progress_label.config(text="0%")
        self.log("开始 OTA 升级...")

        def do_upgrade():
            try:
                data = self.firmware_data
                size = len(data)
                crc = binascii.crc32(data) & 0xFFFFFFFF

                self.root.after(0, lambda: self.log("进入 Boot 模式..."))
                resp = self.send_cmd("ota_enter", 1)
                self.root.after(0, lambda: self.log(f"  {resp}"))
                time.sleep(2)
                self.ser.reset_input_buffer()

                self.root.after(0, lambda: self.log("确认 Bootloader..."))
                ver = self.send_cmd("version")
                if "Bootloader" not in ver:
                    self.root.after(0, lambda: self.log("错误: 未进入 Bootloader"))
                    self.root.after(0, self.upgrade_fail)
                    return
                self.root.after(0, lambda: self.log(f"  {ver}"))

                self.root.after(0, lambda: self.log(f"开始 OTA ({size} bytes)..."))
                resp = self.send_cmd(f"ota_start {size}", 1)
                self.root.after(0, lambda: self.log(f"  {resp}"))

                chunk_size = 128
                sent = 0
                for i in range(0, size, chunk_size):
                    if not self.upgrading:
                        self.root.after(0, self.upgrade_cancelled)
                        return
                    chunk = data[i:i+chunk_size]
                    hex_data = chunk.hex()
                    self.ser.write(f"ota_data {hex_data}\r\n".encode())
                    time.sleep(0.2)
                    sent += len(chunk)
                    progress = sent * 100 // size
                    self.root.after(0, lambda p=progress: self.progress.configure(value=p))
                    self.root.after(0, lambda p=progress: self.progress_label.config(text=f"{p}%"))

                self.root.after(0, lambda: self.log(f"发送完成 ({sent} bytes)"))
                self.root.after(0, lambda: self.log("校验固件..."))
                resp = self.send_cmd(f"ota_end {crc:08X}", 3)
                self.root.after(0, lambda: self.log(f"  {resp}"))

                if "match=YES" in resp or "OK" in resp:
                    self.root.after(0, self.upgrade_success)
                else:
                    self.root.after(0, self.upgrade_fail)

            except Exception as e:
                self.root.after(0, lambda: self.log(f"错误: {e}"))
                self.root.after(0, self.upgrade_fail)

        threading.Thread(target=do_upgrade, daemon=True).start()

    def upgrade_success(self):
        self.upgrading = False
        self.upgrade_btn.config(state="normal")
        self.cancel_btn.config(state="disabled")
        self.progress['value'] = 100
        self.progress_label.config(text="100%")
        self.log("OTA 升级成功！")

    def upgrade_fail(self):
        self.upgrading = False
        self.upgrade_btn.config(state="normal")
        self.cancel_btn.config(state="disabled")
        self.log("OTA 升级失败")

    def cancel_upgrade(self):
        self.upgrading = False
        self.upgrade_btn.config(state="normal")
        self.cancel_btn.config(state="disabled")
        self.progress['value'] = 0
        self.progress_label.config(text="0%")
        self.log("已取消")

    def get_version(self):
        if not self.connected:
            self.log("未连接")
            return
        resp = self.send_cmd("version")
        self.log(f"版本: {resp}")

    def get_status(self):
        if not self.connected:
            self.log("未连接")
            return
        resp = self.send_cmd("ota_status")
        self.log(f"状态: {resp}")

    def test_led(self):
        if not self.connected:
            self.log("未连接")
            return
        self.send_cmd("led_on")
        self.log("LED ON")
        time.sleep(0.5)
        self.send_cmd("led_off")
        self.log("LED OFF")

    def log(self, msg):
        timestamp = time.strftime("%H:%M:%S")
        self.log_text.insert("end", f"[{timestamp}] {msg}\n")
        self.log_text.see("end")

    def clear_log(self):
        self.log_text.delete("1.0", "end")

    def run(self):
        self.root.mainloop()


if __name__ == '__main__':
    OTAApp().run()
