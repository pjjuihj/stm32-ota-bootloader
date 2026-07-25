#!/usr/bin/env python3
"""
OTA Bootloader Tool - 简洁现代风格
"""

import customtkinter as ctk
import time
import threading

ctk.set_appearance_mode("light")
ctk.set_default_color_theme("blue")

class OTAToolApp:
    def __init__(self):
        self.root = ctk.CTk()
        self.root.title("OTA Tool")
        self.root.geometry("500x650")
        self.root.resizable(False, False)

        self.connected = False
        self.upgrading = False

        self.create_ui()

    def create_ui(self):
        # 主容器 - 居中布局
        container = ctk.CTkFrame(self.root, fg_color="#f5f5f5")
        container.pack(fill="both", expand=True, padx=0, pady=0)

        # ===== 顶部状态栏 =====
        top_bar = ctk.CTkFrame(container, fg_color="#2196F3", height=60, corner_radius=0)
        top_bar.pack(fill="x")
        top_bar.pack_propagate(False)

        ctk.CTkLabel(top_bar, text="OTA Tool",
                     font=ctk.CTkFont(size=20, weight="bold"),
                     text_color="white").pack(side="left", padx=20, pady=15)

        self.status_dot = ctk.CTkLabel(top_bar, text="●",
                                       text_color="#9E9E9E",
                                       font=ctk.CTkFont(size=16))
        self.status_dot.pack(side="right", padx=20)

        # ===== 主内容区 =====
        content = ctk.CTkFrame(container, fg_color="#f5f5f5")
        content.pack(fill="both", expand=True, padx=20, pady=20)

        # --- 串口连接 ---
        section1 = ctk.CTkFrame(content, fg_color="white", corner_radius=12)
        section1.pack(fill="x", pady=(0, 15))

        ctk.CTkLabel(section1, text="串口",
                     font=ctk.CTkFont(size=13, weight="bold"),
                     text_color="#333").pack(anchor="w", padx=15, pady=(15, 5))

        row1 = ctk.CTkFrame(section1, fg_color="transparent")
        row1.pack(fill="x", padx=15, pady=(0, 15))

        self.port_menu = ctk.CTkOptionMenu(row1, values=["COM3", "COM4", "COM5"],
                                           width=120, height=36,
                                           fg_color="#E0E0E0",
                                           button_color="#BDBDBD",
                                           button_hover_color="#9E9E9E",
                                           text_color="#333")
        self.port_menu.pack(side="left", padx=(0, 8))
        self.port_menu.set("COM3")

        self.connect_btn = ctk.CTkButton(row1, text="连接", width=80, height=36,
                                         fg_color="#4CAF50", hover_color="#388E3C",
                                         command=self.toggle_connect)
        self.connect_btn.pack(side="left")

        # --- 固件选择 ---
        section2 = ctk.CTkFrame(content, fg_color="white", corner_radius=12)
        section2.pack(fill="x", pady=(0, 15))

        ctk.CTkLabel(section2, text="固件",
                     font=ctk.CTkFont(size=13, weight="bold"),
                     text_color="#333").pack(anchor="w", padx=15, pady=(15, 5))

        row2 = ctk.CTkFrame(section2, fg_color="transparent")
        row2.pack(fill="x", padx=15, pady=(0, 15))

        self.fw_entry = ctk.CTkEntry(row2, placeholder_text="选择固件文件",
                                     height=36, fg_color="#F5F5F5",
                                     border_color="#E0E0E0", text_color="#333")
        self.fw_entry.pack(side="left", fill="x", expand=True, padx=(0, 8))

        ctk.CTkButton(row2, text="浏览", width=70, height=36,
                      fg_color="#2196F3", hover_color="#1976D2",
                      command=self.browse_file).pack(side="left")

        self.fw_info = ctk.CTkLabel(section2, text="",
                                    text_color="#757575",
                                    font=ctk.CTkFont(size=11))
        self.fw_info.pack(anchor="w", padx=15, pady=(0, 10))

        # --- 操作按钮 ---
        section3 = ctk.CTkFrame(content, fg_color="white", corner_radius=12)
        section3.pack(fill="x", pady=(0, 15))

        btn_row = ctk.CTkFrame(section3, fg_color="transparent")
        btn_row.pack(fill="x", padx=15, pady=15)

        self.upgrade_btn = ctk.CTkButton(btn_row, text="开始升级", width=140, height=44,
                                         fg_color="#FF5722", hover_color="#E64A19",
                                         font=ctk.CTkFont(size=14, weight="bold"),
                                         command=self.start_upgrade)
        self.upgrade_btn.pack(side="left", padx=(0, 10))

        self.cancel_btn = ctk.CTkButton(btn_row, text="取消", width=80, height=44,
                                        fg_color="#9E9E9E", hover_color="#757575",
                                        command=self.cancel_upgrade, state="disabled")
        self.cancel_btn.pack(side="left")

        # 右侧辅助按钮
        for text, cmd in [("版本", self.get_version), ("状态", self.get_status)]:
            ctk.CTkButton(btn_row, text=text, width=65, height=36,
                          fg_color="#E0E0E0", hover_color="#BDBDBD",
                          text_color="#333",
                          command=cmd).pack(side="right", padx=3)

        # --- 进度条 ---
        section4 = ctk.CTkFrame(content, fg_color="white", corner_radius=12)
        section4.pack(fill="x", pady=(0, 15))

        progress_inner = ctk.CTkFrame(section4, fg_color="transparent")
        progress_inner.pack(fill="x", padx=15, pady=15)

        self.progress_bar = ctk.CTkProgressBar(progress_inner, height=8,
                                               fg_color="#E0E0E0",
                                               progress_color="#2196F3")
        self.progress_bar.pack(side="left", fill="x", expand=True, padx=(0, 10))
        self.progress_bar.set(0)

        self.progress_label = ctk.CTkLabel(progress_inner, text="0%",
                                          font=ctk.CTkFont(size=14, weight="bold"),
                                          text_color="#333")
        self.progress_label.pack(side="left")

        # --- 日志 ---
        section5 = ctk.CTkFrame(content, fg_color="white", corner_radius=12)
        section5.pack(fill="both", expand=True)

        log_header = ctk.CTkFrame(section5, fg_color="transparent")
        log_header.pack(fill="x", padx=15, pady=(15, 5))

        ctk.CTkLabel(log_header, text="日志",
                     font=ctk.CTkFont(size=13, weight="bold"),
                     text_color="#333").pack(side="left")

        ctk.CTkButton(log_header, text="清除", width=55, height=28,
                      fg_color="#E0E0E0", hover_color="#BDBDBD",
                      text_color="#333",
                      command=self.clear_log).pack(side="right")

        self.log_text = ctk.CTkTextbox(section5, height=120,
                                       fg_color="#FAFAFA",
                                       text_color="#424242",
                                       font=ctk.CTkFont(family="Consolas", size=11),
                                       border_width=1,
                                       border_color="#E0E0E0")
        self.log_text.pack(fill="both", expand=True, padx=15, pady=(0, 15))

        self.log("就绪")

    def toggle_connect(self):
        if self.connected:
            self.disconnect()
        else:
            self.connect()

    def connect(self):
        port = self.port_menu.get()
        self.connect_btn.configure(text="...", state="disabled")
        self.status_dot.configure(text_color="#FFC107")

        def do_connect():
            time.sleep(0.8)
            self.root.after(0, lambda: self.connect_done(port))

        threading.Thread(target=do_connect, daemon=True).start()

    def connect_done(self, port):
        self.connected = True
        self.connect_btn.configure(text="断开", state="normal",
                                   fg_color="#F44336", hover_color="#D32F2F")
        self.status_dot.configure(text_color="#4CAF50")
        self.log(f"已连接 {port}")

    def disconnect(self):
        self.connected = False
        self.connect_btn.configure(text="连接", state="normal",
                                   fg_color="#4CAF50", hover_color="#388E3C")
        self.status_dot.configure(text_color="#9E9E9E")
        self.log("已断开")

    def browse_file(self):
        from tkinter import filedialog
        path = filedialog.askopenfilename(
            filetypes=[("HEX", "*.hex"), ("All", "*.*")]
        )
        if path:
            self.fw_entry.delete(0, "end")
            self.fw_entry.insert(0, path)
            name = path.split("/")[-1].split("\\")[-1]
            self.fw_info.configure(text=f"{name} | 12.4 KB | CRC: 0xB01FF592")

    def start_upgrade(self):
        if not self.connected:
            self.log("请先连接串口")
            return

        self.upgrading = True
        self.upgrade_btn.configure(state="disabled")
        self.cancel_btn.configure(state="normal")
        self.progress_bar.set(0)
        self.progress_label.configure(text="0%")

        self.log("开始升级...")

        def upgrade():
            for i in range(101):
                if not self.upgrading:
                    break
                time.sleep(0.04)
                self.root.after(0, lambda v=i: self.progress_bar.set(v/100))
                self.root.after(0, lambda v=i: self.progress_label.configure(text=f"{v}%"))
            self.root.after(0, self.upgrade_done)

        threading.Thread(target=upgrade, daemon=True).start()

    def upgrade_done(self):
        self.upgrading = False
        self.upgrade_btn.configure(state="normal")
        self.cancel_btn.configure(state="disabled")
        self.progress_bar.set(1.0)
        self.progress_label.configure(text="100%")
        self.log("升级成功！")

    def cancel_upgrade(self):
        self.upgrading = False
        self.upgrade_btn.configure(state="normal")
        self.cancel_btn.configure(state="disabled")
        self.progress_bar.set(0)
        self.progress_label.configure(text="0%")
        self.log("已取消")

    def get_version(self):
        if self.connected:
            self.log("Bootloader v1.0.0")
        else:
            self.log("未连接")

    def get_status(self):
        if self.connected:
            self.log("State: IDLE")
        else:
            self.log("未连接")

    def log(self, msg):
        timestamp = time.strftime("%H:%M:%S")
        self.log_text.insert("end", f"[{timestamp}] {msg}\n")
        self.log_text.see("end")

    def clear_log(self):
        self.log_text.delete("1.0", "end")

    def run(self):
        self.root.mainloop()


if __name__ == '__main__':
    OTAToolApp().run()
