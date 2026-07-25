#!/usr/bin/env python3
"""
OTA Bootloader Tool v2 - 真实可用版本
支持真实串口连接和 OTA 升级
"""

import customtkinter as ctk
import serial
import serial.tools.list_ports
import time
import binascii
import threading
import os
from tkinter import filedialog

ctk.set_appearance_mode("light")
ctk.set_default_color_theme("blue")


def read_hex_file(path):
    """读取 HEX 文件"""
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


class SerialPort:
    """串口封装"""
    def __init__(self):
        self.ser = None
        self.connected = False

    def connect(self, port, baud):
        try:
            self.ser = serial.Serial(port, baud, timeout=2)
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

    def send_cmd(self, cmd, wait=0.5):
        if not self.connected:
            return ""
        self.ser.reset_input_buffer()
        self.ser.write((cmd + '\r\n').encode())
        time.sleep(wait)
        return self.ser.read(self.ser.in_waiting).decode('utf-8', errors='ignore').strip()


class OTAApp:
    def __init__(self):
        self.root = ctk.CTk()
        self.root.title("OTA Tool")
        self.root.geometry("500x650")
        self.root.resizable(False, False)

        self.serial = SerialPort()
        self.connected = False
        self.upgrading = False
        self.firmware_data = None
        self.firmware_path = ""

        self.create_ui()
        self.refresh_ports()

    def create_ui(self):
        container = ctk.CTkFrame(self.root, fg_color="#f5f5f5")
        container.pack(fill="both", expand=True)

        # 顶部
        top = ctk.CTkFrame(container, fg_color="#2196F3", height=60, corner_radius=0)
        top.pack(fill="x")
        top.pack_propagate(False)
        ctk.CTkLabel(top, text="OTA Tool", font=ctk.CTkFont(size=20, weight="bold"),
                     text_color="white").pack(side="left", padx=20, pady=15)
        self.status_dot = ctk.CTkLabel(top, text="●", text_color="#9E9E9E",
                                       font=ctk.CTkFont(size=16))
        self.status_dot.pack(side="right", padx=20)

        content = ctk.CTkFrame(container, fg_color="#f5f5f5")
        content.pack(fill="both", expand=True, padx=20, pady=20)

        # 串口
        s1 = ctk.CTkFrame(content, fg_color="white", corner_radius=12)
        s1.pack(fill="x", pady=(0, 15))
        ctk.CTkLabel(s1, text="串口", font=ctk.CTkFont(size=13, weight="bold"),
                     text_color="#333").pack(anchor="w", padx=15, pady=(15, 5))
        r1 = ctk.CTkFrame(s1, fg_color="transparent")
        r1.pack(fill="x", padx=15, pady=(0, 15))
        self.port_menu = ctk.CTkOptionMenu(r1, values=[""], width=140, height=36,
                                           fg_color="#E0E0E0", button_color="#BDBDBD",
                                           button_hover_color="#9E9E9E", text_color="#333")
        self.port_menu.pack(side="left", padx=(0, 8))
        ctk.CTkButton(r1, text="刷新", width=60, height=36, fg_color="#E0E0E0",
                      hover_color="#BDBDBD", text_color="#333",
                      command=self.refresh_ports).pack(side="left", padx=(0, 8))
        self.connect_btn = ctk.CTkButton(r1, text="连接", width=80, height=36,
                                         fg_color="#4CAF50", hover_color="#388E3C",
                                         command=self.toggle_connect)
        self.connect_btn.pack(side="left")

        # 固件
        s2 = ctk.CTkFrame(content, fg_color="white", corner_radius=12)
        s2.pack(fill="x", pady=(0, 15))
        ctk.CTkLabel(s2, text="固件", font=ctk.CTkFont(size=13, weight="bold"),
                     text_color="#333").pack(anchor="w", padx=15, pady=(15, 5))
        r2 = ctk.CTkFrame(s2, fg_color="transparent")
        r2.pack(fill="x", padx=15, pady=(0, 15))
        self.fw_entry = ctk.CTkEntry(r2, placeholder_text="选择固件文件", height=36,
                                     fg_color="#F5F5F5", border_color="#E0E0E0", text_color="#333")
        self.fw_entry.pack(side="left", fill="x", expand=True, padx=(0, 8))
        ctk.CTkButton(r2, text="浏览", width=70, height=36, fg_color="#2196F3",
                      hover_color="#1976D2", command=self.browse_file).pack(side="left")
        self.fw_info = ctk.CTkLabel(s2, text="", text_color="#757575",
                                    font=ctk.CTkFont(size=11))
        self.fw_info.pack(anchor="w", padx=15, pady=(0, 10))

        # 按钮
        s3 = ctk.CTkFrame(content, fg_color="white", corner_radius=12)
        s3.pack(fill="x", pady=(0, 15))
        br = ctk.CTkFrame(s3, fg_color="transparent")
        br.pack(fill="x", padx=15, pady=15)
        self.upgrade_btn = ctk.CTkButton(br, text="开始升级", width=140, height=44,
                                         fg_color="#FF5722", hover_color="#E64A19",
                                         font=ctk.CTkFont(size=14, weight="bold"),
                                         command=self.start_upgrade)
        self.upgrade_btn.pack(side="left", padx=(0, 10))
        self.cancel_btn = ctk.CTkButton(br, text="取消", width=80, height=44,
                                        fg_color="#9E9E9E", hover_color="#757575",
                                        command=self.cancel_upgrade, state="disabled")
        self.cancel_btn.pack(side="left")
        ctk.CTkButton(br, text="版本", width=65, height=36, fg_color="#E0E0E0",
                      hover_color="#BDBDBD", text_color="#333",
                      command=self.get_version).pack(side="right", padx=3)

        # 进度
        s4 = ctk.CTkFrame(content, fg_color="white", corner_radius=12)
        s4.pack(fill="x", pady=(0, 15))
        pi = ctk.CTkFrame(s4, fg_color="transparent")
        pi.pack(fill="x", padx=15, pady=15)
        self.progress_bar = ctk.CTkProgressBar(pi, height=8, fg_color="#E0E0E0",
                                               progress_color="#2196F3")
        self.progress_bar.pack(side="left", fill="x", expand=True, padx=(0, 10))
        self.progress_bar.set(0)
        self.progress_label = ctk.CTkLabel(pi, text="0%", font=ctk.CTkFont(size=14, weight="bold"),
                                          text_color="#333")
        self.progress_label.pack(side="left")
        self.status_text = ctk.CTkLabel(s4, text="就绪", text_color="#757575",
                                        font=ctk.CTkFont(size=11))
        self.status_text.pack(anchor="w", padx=15, pady=(0, 10))

        # 日志
        s5 = ctk.CTkFrame(content, fg_color="white", corner_radius=12)
        s5.pack(fill="both", expand=True)
        lh = ctk.CTkFrame(s5, fg_color="transparent")
        lh.pack(fill="x", padx=15, pady=(15, 5))
        ctk.CTkLabel(lh, text="日志", font=ctk.CTkFont(size=13, weight="bold"),
                     text_color="#333").pack(side="left")
        ctk.CTkButton(lh, text="清除", width=55, height=28, fg_color="#E0E0E0",
                      hover_color="#BDBDBD", text_color="#333",
                      command=self.clear_log).pack(side="right")
        self.log_text = ctk.CTkTextbox(s5, height=120, fg_color="#FAFAFA",
                                       text_color="#424242",
                                       font=ctk.CTkFont(family="Consolas", size=11),
                                       border_width=1, border_color="#E0E0E0")
        self.log_text.pack(fill="both", expand=True, padx=15, pady=(0, 15))
        self.log("就绪")
        self.log("💡 F5=刷新串口  Ctrl+R=开始升级  Esc=取消")

        # 绑定快捷键
        self.root.bind("<F5>", lambda e: self.refresh_ports())
        self.root.bind("<Control-r>", lambda e: self.start_upgrade())
        self.root.bind("<Escape>", lambda e: self.cancel_upgrade())

    def refresh_ports(self):
        ports = serial.tools.list_ports.comports()
        port_list = [p.device for p in ports]
        self.port_menu.configure(values=port_list if port_list else ["无串口"])
        if port_list:
            self.port_menu.set(port_list[0])
        self.log(f"检测到 {len(port_list)} 个串口")

    def toggle_connect(self):
        if self.connected:
            self.disconnect()
        else:
            self.connect()

    def connect(self):
        port = self.port_menu.get()
        if not port or port == "无串口":
            self.show_error("请选择串口", "请从下拉菜单中选择一个可用的串口")
            return

        self.connect_btn.configure(text="连接中...", state="disabled")
        self.status_dot.configure(text_color="#FFC107")
        self.status_text.configure(text="正在连接...", text_color="#FF9800")
        self.log(f"正在连接 {port}...")

        def do_connect():
            baud = 115200
            if self.serial.connect(port, baud):
                self.root.after(0, lambda: self.connect_done(port, baud))
            else:
                self.root.after(0, lambda: self.connect_fail(port))

        threading.Thread(target=do_connect, daemon=True).start()

    def connect_done(self, port, baud):
        self.connected = True
        self.connect_btn.configure(text="断开", state="normal",
                                   fg_color="#F44336", hover_color="#D32F2F")
        self.status_dot.configure(text_color="#4CAF50")
        self.log(f"已连接 {port} @ {baud}")

        # 自动获取版本
        version = self.serial.send_cmd("version")
        if version:
            self.log(f"设备: {version}")

    def connect_fail(self, port=""):
        self.connect_btn.configure(text="连接", state="normal")
        self.status_dot.configure(text_color="#F44336")
        self.status_text.configure(text="连接失败", text_color="#F44336")
        self.log(f"连接 {port} 失败")
        self.log("  可能原因: 端口被占用、设备未连接、波特率错误")
        self.log("  解决方法: 关闭其他串口工具，检查 USB 连接")

    def disconnect(self):
        self.serial.disconnect()
        self.connected = False
        self.connect_btn.configure(text="连接", state="normal",
                                   fg_color="#4CAF50", hover_color="#388E3C")
        self.status_dot.configure(text_color="#9E9E9E")
        self.log("已断开")

    def browse_file(self):
        path = filedialog.askopenfilename(
            filetypes=[("HEX", "*.hex"), ("All", "*.*")]
        )
        if path:
            self.fw_path = path
            self.fw_entry.delete(0, "end")
            self.fw_entry.insert(0, path)
            try:
                data, addr = read_hex_file(path)
                self.firmware_data = data
                crc = binascii.crc32(data) & 0xFFFFFFFF
                name = path.split("/")[-1].split("\\")[-1]
                self.fw_info.configure(text=f"{name} | {len(data)} bytes | 0x{addr:08X} | 0x{crc:08X}")
                self.log(f"已加载: {name} ({len(data)} bytes)")
            except Exception as e:
                self.fw_info.configure(text=f"错误: {e}")
                self.log(f"加载失败: {e}")

    def start_upgrade(self):
        if not self.connected:
            self.log("请先连接串口")
            return
        if not self.firmware_data:
            self.log("请选择固件文件")
            return

        self.upgrading = True
        self.upgrade_btn.configure(state="disabled")
        self.cancel_btn.configure(state="normal")
        self.progress_bar.set(0)
        self.progress_label.configure(text="0%")
        self.status_text.configure(text="升级中...", text_color="#2196F3")
        self.log("🚀 开始 OTA 升级...")

        def do_upgrade():
            try:
                data = self.firmware_data
                size = len(data)
                crc = binascii.crc32(data) & 0xFFFFFFFF

                self.root.after(0, lambda: self.log("进入 Boot 模式..."))
                resp = self.serial.send_cmd("ota_enter", 1)
                self.root.after(0, lambda: self.log(f"  {resp}"))
                time.sleep(2)
                self.serial.ser.reset_input_buffer()

                self.root.after(0, lambda: self.log("确认 Bootloader..."))
                ver = self.serial.send_cmd("version")
                if "Bootloader" not in ver:
                    self.root.after(0, lambda: self.log("错误: 未进入 Bootloader"))
                    self.root.after(0, self.upgrade_fail)
                    return
                self.root.after(0, lambda: self.log(f"  {ver}"))

                self.root.after(0, lambda: self.log(f"开始 OTA ({size} bytes)..."))
                resp = self.serial.send_cmd(f"ota_start {size}", 1)
                self.root.after(0, lambda: self.log(f"  {resp}"))

                # 发送固件
                chunk_size = 128
                sent = 0
                for i in range(0, size, chunk_size):
                    if not self.upgrading:
                        self.root.after(0, lambda: self.log("已取消"))
                        self.root.after(0, self.upgrade_cancelled)
                        return
                    chunk = data[i:i+chunk_size]
                    hex_data = chunk.hex()
                    self.serial.send_cmd(f"ota_data {hex_data}", 0.3)
                    sent += len(chunk)
                    progress = sent * 100 // size
                    self.root.after(0, lambda p=progress: self.progress_bar.set(p/100))
                    self.root.after(0, lambda p=progress: self.progress_label.configure(text=f"{p}%"))

                self.root.after(0, lambda: self.log(f"发送完成 ({sent} bytes)"))

                # 结束 OTA
                self.root.after(0, lambda: self.log("校验固件..."))
                resp = self.serial.send_cmd(f"ota_end {crc:08X}", 2)
                self.root.after(0, lambda: self.log(f"  {resp}"))

                if "match=YES" in resp or "OK" in resp:
                    self.root.after(0, self.upgrade_success)
                else:
                    self.root.after(0, self.upgrade_fail)

            except Exception as e:
                self.root.after(0, lambda: self.log(f"❌ 错误: {e}"))
                self.root.after(0, lambda: self.log("  可能原因: 设备断开、固件损坏"))
                self.root.after(0, lambda: self.log("  解决方法: 检查连接，重新选择固件"))
                self.root.after(0, self.upgrade_fail)

        threading.Thread(target=do_upgrade, daemon=True).start()

    def upgrade_success(self):
        self.upgrading = False
        self.upgrade_btn.configure(state="normal")
        self.cancel_btn.configure(state="disabled")
        self.progress_bar.set(1.0)
        self.progress_label.configure(text="100%")
        self.status_text.configure(text="升级成功！", text_color="#4CAF50")
        self.log("OTA 升级成功！")

    def upgrade_fail(self):
        self.upgrading = False
        self.upgrade_btn.configure(state="normal")
        self.cancel_btn.configure(state="disabled")
        self.status_text.configure(text="升级失败", text_color="#F44336")
        self.log("OTA 升级失败")

    def cancel_upgrade(self):
        self.upgrading = False
        self.upgrade_btn.configure(state="normal")
        self.cancel_btn.configure(state="disabled")
        self.progress_bar.set(0)
        self.progress_label.configure(text="0%")
        self.status_text.configure(text="已取消", text_color="#757575")
        self.log("已取消")

    def get_version(self):
        if not self.connected:
            self.log("❌ 请先连接串口")
            return
        resp = self.serial.send_cmd("version")
        self.log(f"📋 版本: {resp}")

    def show_help(self):
        help_text = """
OTA Tool 使用指南:

1. 连接设备:
   - 选择串口 (如 COM3)
   - 点击"连接"按钮
   - 等待连接成功

2. 加载固件:
   - 点击"浏览"选择 HEX 文件
   - 确认固件信息正确

3. 开始升级:
   - 点击"开始升级"
   - 等待进度完成
   - 查看日志确认结果

快捷键:
- F5: 刷新串口列表
- Ctrl+R: 开始升级
- Esc: 取消操作
"""
        self.log(help_text)

    def log(self, msg):
        timestamp = time.strftime("%H:%M:%S")
        self.log_text.insert("end", f"[{timestamp}] {msg}\n")
        self.log_text.see("end")

    def clear_log(self):
        self.log_text.delete("1.0", "end")

    def show_error(self, title, message):
        from tkinter import messagebox
        messagebox.showerror(title, message)

    def run(self):
        self.root.mainloop()


if __name__ == '__main__':
    OTAApp().run()
