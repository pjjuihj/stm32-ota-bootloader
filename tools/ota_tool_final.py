#!/usr/bin/env python3
"""
OTA Bootloader Tool - 最终版本
融合 GUI 界面 + 可靠的串口通信
"""

import customtkinter as ctk
import serial
import serial.tools.list_ports
import time
import binascii
import threading
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


class OTAApp:
    def __init__(self):
        self.root = ctk.CTk()
        self.root.title("OTA Bootloader Tool")
        self.root.geometry("900x600")
        self.root.minsize(800, 500)

        self.ser = None
        self.connected = False
        self.upgrading = False
        self.firmware_data = None

        self.create_ui()
        self.refresh_ports()

    def create_ui(self):
        main = ctk.CTkFrame(self.root, fg_color="#f0f0f0")
        main.pack(fill="both", expand=True)

        # ===== 左侧边栏 =====
        sidebar = ctk.CTkFrame(main, fg_color="white", width=200, corner_radius=0)
        sidebar.pack(side="left", fill="y")
        sidebar.pack_propagate(False)

        header = ctk.CTkFrame(sidebar, fg_color="#2196F3", height=45, corner_radius=0)
        header.pack(fill="x")
        header.pack_propagate(False)
        ctk.CTkLabel(header, text="OTA Tool", font=ctk.CTkFont(size=15, weight="bold"),
                     text_color="white").pack(pady=10)

        cfg = ctk.CTkFrame(sidebar, fg_color="transparent")
        cfg.pack(fill="x", padx=10, pady=(10, 0))

        ctk.CTkLabel(cfg, text="串口配置", font=ctk.CTkFont(size=11, weight="bold"),
                     text_color="#333").pack(anchor="w", pady=(0, 5))

        ctk.CTkLabel(cfg, text="端口号", text_color="#666", font=ctk.CTkFont(size=10)).pack(anchor="w")
        self.port_menu = ctk.CTkOptionMenu(cfg, values=[""], width=160, height=28,
                                           fg_color="#E8E8E8", button_color="#D0D0D0", text_color="#333")
        self.port_menu.pack(fill="x", pady=(0, 5))

        ctk.CTkLabel(cfg, text="波特率", text_color="#666", font=ctk.CTkFont(size=10)).pack(anchor="w")
        self.baud_menu = ctk.CTkOptionMenu(cfg, values=["9600", "57600", "115200", "230400", "460800"],
                                           width=160, height=28, fg_color="#E8E8E8",
                                           button_color="#D0D0D0", text_color="#333")
        self.baud_menu.pack(fill="x", pady=(0, 5))
        self.baud_menu.set("115200")

        ctk.CTkLabel(cfg, text="数据位", text_color="#666", font=ctk.CTkFont(size=10)).pack(anchor="w")
        ctk.CTkOptionMenu(cfg, values=["8"], width=160, height=28,
                          fg_color="#E8E8E8", state="disabled").pack(fill="x", pady=(0, 5))

        ctk.CTkLabel(cfg, text="停止位", text_color="#666", font=ctk.CTkFont(size=10)).pack(anchor="w")
        ctk.CTkOptionMenu(cfg, values=["1"], width=160, height=28,
                          fg_color="#E8E8E8", state="disabled").pack(fill="x", pady=(0, 5))

        self.connect_btn = ctk.CTkButton(cfg, text="打开串口", width=160, height=32,
                                         fg_color="#2196F3", hover_color="#1976D2",
                                         font=ctk.CTkFont(size=12, weight="bold"),
                                         command=self.toggle_connect)
        self.connect_btn.pack(fill="x", pady=(10, 0))

        sf = ctk.CTkFrame(cfg, fg_color="transparent")
        sf.pack(fill="x", pady=(5, 0))
        self.status_dot = ctk.CTkLabel(sf, text="●", text_color="#9E9E9E",
                                       font=ctk.CTkFont(size=12))
        self.status_dot.pack(side="left")
        self.status_label = ctk.CTkLabel(sf, text="未连接", text_color="#9E9E9E",
                                         font=ctk.CTkFont(size=10))
        self.status_label.pack(side="left", padx=3)

        # ===== 右侧内容区 =====
        right = ctk.CTkFrame(main, fg_color="#f0f0f0")
        right.pack(side="left", fill="both", expand=True)

        # 顶部工具栏
        toolbar = ctk.CTkFrame(right, fg_color="white", height=40, corner_radius=0)
        toolbar.pack(fill="x")
        toolbar.pack_propagate(False)

        self.tab_buttons = {}
        for text, tab_id in [("OTA 升级", "ota"), ("设备信息", "device"), ("设置", "settings")]:
            btn = ctk.CTkButton(toolbar, text=text, width=90, height=30,
                                fg_color="#E8E8E8", hover_color="#D0D0D0",
                                text_color="#333", font=ctk.CTkFont(size=11),
                                command=lambda t=tab_id: self.switch_tab(t))
            btn.pack(side="left", padx=2, pady=5)
            self.tab_buttons[tab_id] = btn

        ctk.CTkLabel(toolbar, text="●", text_color="#4CAF50",
                     font=ctk.CTkFont(size=10)).pack(side="right", padx=10)
        ctk.CTkLabel(toolbar, text="数据", text_color="#888",
                     font=ctk.CTkFont(size=10)).pack(side="right", padx=3)

        # 内容容器
        self.content_frame = ctk.CTkFrame(right, fg_color="white")
        self.content_frame.pack(fill="both", expand=True, padx=10, pady=10)

        # 创建标签页
        self.tab_ota = ctk.CTkFrame(self.content_frame, fg_color="transparent")
        self.tab_device = ctk.CTkFrame(self.content_frame, fg_color="transparent")
        self.tab_settings = ctk.CTkFrame(self.content_frame, fg_color="transparent")

        self.create_ota_tab()
        self.create_device_tab()
        self.create_settings_tab()

        # 底部日志区
        bottom = ctk.CTkFrame(right, fg_color="white", height=180)
        bottom.pack(fill="x", padx=10, pady=(0, 10))
        bottom.pack_propagate(False)

        bt = ctk.CTkFrame(bottom, fg_color="#f5f5f5", height=30, corner_radius=0)
        bt.pack(fill="x")
        bt.pack_propagate(False)

        for text in ["Hex", "Rx", "Tx"]:
            ctk.CTkButton(bt, text=text, width=40, height=22,
                          fg_color="#E0E0E0" if text != "Rx" else "#4CAF50",
                          hover_color="#D0D0D0", text_color="white" if text == "Rx" else "#333",
                          font=ctk.CTkFont(size=9)).pack(side="left", padx=2, pady=4)

        ctk.CTkLabel(bt, text="UTF-8", text_color="#888",
                     font=ctk.CTkFont(size=9)).pack(side="right", padx=8)
        ctk.CTkButton(bt, text="清除", width=45, height=22,
                      fg_color="#E0E0E0", hover_color="#D0D0D0",
                      text_color="#333", font=ctk.CTkFont(size=9),
                      command=self.clear_log).pack(side="right", padx=2, pady=4)

        self.log_text = ctk.CTkTextbox(bottom, fg_color="white", text_color="#333",
                                       font=ctk.CTkFont(family="Consolas", size=10), border_width=0)
        self.log_text.pack(fill="both", expand=True, padx=5, pady=3)

        self.log("OTA Bootloader Tool 就绪")
        self.log("F5=刷新串口  Ctrl+R=开始升级  Esc=取消")

        self.root.bind("<F5>", lambda e: self.refresh_ports())
        self.root.bind("<Control-r>", lambda e: self.start_upgrade())
        self.root.bind("<Escape>", lambda e: self.cancel_upgrade())

    def create_ota_tab(self):
        fw = ctk.CTkFrame(self.tab_ota, fg_color="#f8f8f8", corner_radius=8)
        fw.pack(fill="x", padx=10, pady=(10, 5))

        ctk.CTkLabel(fw, text="固件文件", font=ctk.CTkFont(size=11, weight="bold"),
                     text_color="#333").pack(anchor="w", padx=10, pady=(8, 3))

        fr = ctk.CTkFrame(fw, fg_color="transparent")
        fr.pack(fill="x", padx=10, pady=(0, 8))

        self.fw_entry = ctk.CTkEntry(fr, placeholder_text="选择 HEX 固件文件...",
                                     height=28, fg_color="white", border_color="#DDD")
        self.fw_entry.pack(side="left", fill="x", expand=True, padx=(0, 5))
        ctk.CTkButton(fr, text="浏览", width=60, height=28, fg_color="#2196F3",
                      hover_color="#1976D2", command=self.browse_file).pack(side="left")

        self.fw_info = ctk.CTkLabel(fw, text="", text_color="#888",
                                    font=ctk.CTkFont(size=10))
        self.fw_info.pack(anchor="w", padx=10, pady=(0, 5))

        bf = ctk.CTkFrame(self.tab_ota, fg_color="transparent")
        bf.pack(fill="x", padx=10, pady=5)

        self.upgrade_btn = ctk.CTkButton(bf, text="开始升级", width=110, height=36,
                                         fg_color="#FF5722", hover_color="#E64A19",
                                         font=ctk.CTkFont(size=12, weight="bold"),
                                         command=self.start_upgrade)
        self.upgrade_btn.pack(side="left", padx=(0, 6))

        self.cancel_btn = ctk.CTkButton(bf, text="取消", width=60, height=36,
                                        fg_color="#9E9E9E", hover_color="#757575",
                                        command=self.cancel_upgrade, state="disabled")
        self.cancel_btn.pack(side="left", padx=(0, 12))

        for text, cmd in [("版本", self.get_version), ("状态", self.get_status),
                          ("LED", self.test_led)]:
            ctk.CTkButton(bf, text=text, width=55, height=30, fg_color="#E0E0E0",
                          hover_color="#D0D0D0", text_color="#333",
                          font=ctk.CTkFont(size=10), command=cmd).pack(side="left", padx=2)

        pf = ctk.CTkFrame(self.tab_ota, fg_color="#f8f8f8", corner_radius=8)
        pf.pack(fill="x", padx=10, pady=5)

        pr = ctk.CTkFrame(pf, fg_color="transparent")
        pr.pack(fill="x", padx=10, pady=8)

        self.progress_bar = ctk.CTkProgressBar(pr, height=6, fg_color="#E0E0E0",
                                               progress_color="#2196F3")
        self.progress_bar.pack(side="left", fill="x", expand=True, padx=(0, 8))
        self.progress_bar.set(0)

        self.progress_label = ctk.CTkLabel(pr, text="0%", font=ctk.CTkFont(size=11, weight="bold"))
        self.progress_label.pack(side="left")

        self.status_text = ctk.CTkLabel(pf, text="就绪", text_color="#888",
                                        font=ctk.CTkFont(size=10))
        self.status_text.pack(anchor="w", padx=10, pady=(0, 5))

    def create_device_tab(self):
        info_frame = ctk.CTkFrame(self.tab_device, fg_color="#f8f8f8", corner_radius=8)
        info_frame.pack(fill="x", padx=10, pady=10)

        ctk.CTkLabel(info_frame, text="设备信息", font=ctk.CTkFont(size=14, weight="bold"),
                     text_color="#333").pack(anchor="w", padx=15, pady=(15, 10))

        for label, value in [("设备类型", "STM32F407VETx"), ("Bootloader", "v1.0.0"),
                             ("构建日期", "2026-07-20"), ("活动分区", "0x0800C000 (A)"),
                             ("目标分区", "0x08040000 (B)"), ("Flash", "512KB")]:
            row = ctk.CTkFrame(info_frame, fg_color="transparent")
            row.pack(fill="x", padx=15, pady=2)
            ctk.CTkLabel(row, text=label, text_color="#666", width=120, anchor="w").pack(side="left")
            ctk.CTkLabel(row, text=value, text_color="#333", font=ctk.CTkFont(weight="bold")).pack(side="left")

    def create_settings_tab(self):
        settings_frame = ctk.CTkFrame(self.tab_settings, fg_color="#f8f8f8", corner_radius=8)
        settings_frame.pack(fill="x", padx=10, pady=10)

        ctk.CTkLabel(settings_frame, text="设置", font=ctk.CTkFont(size=14, weight="bold"),
                     text_color="#333").pack(anchor="w", padx=15, pady=(15, 10))

        for text in ["启动时自动连接", "连接后自动获取设备信息", "升级后自动保存日志"]:
            frame = ctk.CTkFrame(settings_frame, fg_color="transparent")
            frame.pack(fill="x", padx=15, pady=5)
            ctk.CTkLabel(frame, text=text, text_color="#333").pack(side="left")
            ctk.CTkSwitch(frame, text="", onvalue=True, offvalue=False).pack(side="right")

        about = ctk.CTkFrame(settings_frame, fg_color="transparent")
        about.pack(fill="x", padx=15, pady=15)
        ctk.CTkLabel(about, text="OTA Bootloader Tool v2.0",
                     font=ctk.CTkFont(size=12, weight="bold"), text_color="#333").pack(anchor="w")
        ctk.CTkLabel(about, text="STM32 OTA 升级工具", text_color="#888").pack(anchor="w")

    def switch_tab(self, tab_id):
        self.current_tab = tab_id
        self.tab_ota.pack_forget()
        self.tab_device.pack_forget()
        self.tab_settings.pack_forget()
        if tab_id == "ota":
            self.tab_ota.pack(fill="both", expand=True)
        elif tab_id == "device":
            self.tab_device.pack(fill="both", expand=True)
        elif tab_id == "settings":
            self.tab_settings.pack(fill="both", expand=True)
        for tid, btn in self.tab_buttons.items():
            btn.configure(fg_color="#2196F3" if tid == tab_id else "#E8E8E8",
                         text_color="white" if tid == tab_id else "#333")

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
            self.log("请选择串口")
            return
        self.connect_btn.configure(text="连接中...", state="disabled")
        self.status_dot.configure(text_color="#FFC107")
        self.log(f"正在连接 {port}...")

        def do_connect():
            try:
                baud = int(self.baud_menu.get())
                self.ser = serial.Serial(port, baud, timeout=2)
                time.sleep(0.5)
                self.ser.reset_input_buffer()
                self.connected = True
                self.root.after(0, lambda: self.connect_done(port, baud))
            except Exception as e:
                self.root.after(0, lambda: self.connect_fail(port, str(e)))
        threading.Thread(target=do_connect, daemon=True).start()

    def connect_done(self, port, baud):
        self.connect_btn.configure(text="关闭串口", state="normal", fg_color="#F44336", hover_color="#D32F2F")
        self.status_dot.configure(text_color="#4CAF50")
        self.status_label.configure(text="已连接", text_color="#4CAF50")
        self.log(f"已连接 {port} @ {baud}")
        ver = self.send_cmd("version")
        if ver:
            self.log(f"设备: {ver}")

    def connect_fail(self, port, error):
        self.connect_btn.configure(text="打开串口", state="normal")
        self.status_dot.configure(text_color="#F44336")
        self.status_label.configure(text="连接失败", text_color="#F44336")
        self.log(f"连接 {port} 失败: {error}")

    def disconnect(self):
        if self.ser and self.ser.is_open:
            self.ser.close()
        self.connected = False
        self.ser = None
        self.connect_btn.configure(text="打开串口", state="normal", fg_color="#2196F3", hover_color="#1976D2")
        self.status_dot.configure(text_color="#9E9E9E")
        self.status_label.configure(text="未连接", text_color="#9E9E9E")
        self.log("已关闭串口")

    def send_cmd(self, cmd, wait=1.0):
        """发送命令并等待响应（与最简工具一致）"""
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
                self.fw_info.configure(text=f"{name} | {len(data)} bytes | 0x{addr:08X} | 0x{crc:08X}")
                self.log(f"已加载: {name} ({len(data)} bytes)")
            except Exception as e:
                self.fw_info.configure(text=f"错误: {e}")

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
        self.log("开始 OTA 升级...")

        # 保存串口参数
        port = self.port_menu.get()
        baud = self.baud_menu.get()
        fw_path = self.fw_entry.get()

        def do_upgrade():
            try:
                # 关闭 GUI 的串口
                if self.ser and self.ser.is_open:
                    self.ser.close()
                    self.ser = None
                    self.connected = False

                # 直接使用最简工具的逻辑
                data = self.firmware_data
                size = len(data)
                crc = binascii.crc32(data) & 0xFFFFFFFF

                # 重新打开串口
                self.ser = serial.Serial(port, int(baud), timeout=2)
                time.sleep(1)
                self.ser.reset_input_buffer()
                self.connected = True

                # 复制最简工具的 send_cmd 函数
                def send_cmd_simple(cmd, wait=1.0):
                    self.ser.reset_input_buffer()
                    self.ser.write((cmd + '\r\n').encode())
                    time.sleep(wait)
                    return self.ser.read(self.ser.in_waiting).decode('utf-8', errors='ignore').strip()

                self.root.after(0, lambda: self.log("进入 Boot 模式..."))
                resp = send_cmd_simple("ota_enter", 1)
                self.root.after(0, lambda: self.log(f"  {resp}"))
                time.sleep(2)
                self.ser.reset_input_buffer()

                self.root.after(0, lambda: self.log("确认 Bootloader..."))
                ver = send_cmd_simple("version")
                if "Bootloader" not in ver:
                    self.root.after(0, lambda: self.log("错误: 未进入 Bootloader"))
                    self.root.after(0, self.upgrade_fail)
                    return
                self.root.after(0, lambda: self.log(f"  {ver}"))

                self.root.after(0, lambda: self.log(f"开始 OTA ({size} bytes)..."))
                resp = send_cmd_simple(f"ota_start {size}", 1)
                self.root.after(0, lambda: self.log(f"  {resp}"))

                # 发送固件（与最简工具完全一致）
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

                self.root.after(0, lambda: self.log(f"发送完成 ({sent} bytes)"))
                self.root.after(0, lambda: self.log("校验固件..."))
                resp = send_cmd_simple(f"ota_end {crc:08X}", 3)
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
        self.status_text.configure(text="已取消", text_color="#888")
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
