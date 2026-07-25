# STM32 OTA Bootloader

基于 STM32F407 的 OTA（Over-The-Air）升级系统，支持 A/B 分区、UART 通信、OLED 显示。

## 功能特性

- ✅ A/B 分区 OTA 升级
- ✅ UART 中断接收（4096 字节环形缓冲区）
- ✅ CRC32 固件校验
- ✅ OLED 状态显示
- ✅ PA0 按键触发 Boot 模式
- ✅ 看门狗保护
- ✅ 错误恢复机制
- ✅ PC 上位机工具（Python GUI）

## 硬件要求

| 组件 | 型号 | 说明 |
|------|------|------|
| MCU | STM32F407VETx | 主控芯片 |
| OLED | 0.96寸 SSD1306 | I2C 接口 |
| 串口 | CH340 USB转串口 | 115200 波特率 |
| 按键 | PA0 | Boot 模式触发 |

## Flash 布局

```
┌──────────────────────────────────────┐
│ Sector 0-1 (32KB)   │ Bootloader     │ 0x08000000
├──────────────────────┼────────────────┤
│ Sector 2 (16KB)      │ 控制数据       │ 0x08008000
├──────────────────────┼────────────────┤
│ Sector 3-5 (208KB)   │ Partition A    │ 0x0800C000
├──────────────────────┼────────────────┤
│ Sector 6-7 (256KB)   │ Partition B    │ 0x08040000
└──────────────────────┴────────────────┘
```

## 快速开始

### 1. 编译 Bootloader

1. 打开 `MDK-ARM/test1.uvprojx`
2. 选择 Target: `test1`
3. 点击 Build (F7)
4. 烧录到开发板

### 2. 使用 OTA 工具

```bash
# 安装依赖
pip install pyserial

# 运行最简版本（推荐）
python tools/ota_tool_simple.py

# 或运行 GUI 版本
python tools/ota_tool_tkinter.py
```

### 3. OTA 升级流程

1. 连接串口（COM3, 115200）
2. 选择 HEX 固件文件
3. 点击"开始升级"
4. 等待升级完成

## OTA 命令协议

| 命令 | 参数 | 说明 |
|------|------|------|
| `version` | 无 | 查看版本信息 |
| `info` | 无 | 查看分区信息 |
| `ota_enter` | 无 | 进入 Boot 模式 |
| `ota_start <size>` | 固件大小 | 开始 OTA 升级 |
| `ota_data <hex>` | HEX 数据 | 发送固件数据 |
| `ota_end <crc>` | CRC32 | 结束 OTA 升级 |
| `ota_status` | 无 | 查看 OTA 状态 |
| `reset` | 无 | 复位设备 |
| `led_on` | 无 | LED 开启 |
| `led_off` | 无 | LED 关闭 |

## 性能参数

| 参数 | 值 |
|------|-----|
| 波特率 | 115200 |
| 块大小 | 128 字节 |
| 传输速度 | ~580 字节/秒 |
| 12KB 固件耗时 | ~21 秒 |

## 文件结构

```
stm32-ota-bootloader/
├── Core/
│   ├── Inc/
│   │   ├── bootloader.h        # Bootloader 接口
│   │   ├── display_ota.h       # OLED 显示接口
│   │   └── main.h              # 主程序头文件
│   └── Src/
│       ├── bootloader.c        # Bootloader 核心
│       ├── display_ota.c       # OLED 显示实现
│       └── main.c              # 主程序入口
├── Drivers/                    # HAL 库
├── docs/                       # 文档
├── tests/                      # 测试脚本
├── tools/
│   ├── ota_tool_simple.py      # 最简版本（推荐）
│   ├── ota_tool_tkinter.py     # tkinter GUI
│   └── ota_tool_final.py       # CustomTkinter GUI
├── MDK-ARM/                    # Keil 工程
└── test1.ioc                   # CubeMX 配置
```

## 开发说明

### 调试技巧

1. **串口调试**：使用串口终端查看 Bootloader 输出
2. **OLED 显示**：观察 OLED 上的状态信息
3. **LED 指示**：LED 闪烁表示 Bootloader 运行中

### 常见问题

| 问题 | 原因 | 解决方案 |
|------|------|----------|
| OTA 失败 | 串口通信不稳定 | 增加延时或降低波特率 |
| CRC 不匹配 | 数据传输损坏 | 检查接线，增加重试 |
| OLED 不显示 | I2C 通信问题 | 检查接线，确认地址 |

## 许可证

MIT License

## 贡献

欢迎提交 Issue 和 Pull Request！
