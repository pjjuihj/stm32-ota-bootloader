# STM32 OTA Bootloader 设计方案

## 1. 项目概述

### 1.1 目标

设计一个基于 STM32F407 的 OTA（Over-The-Air）升级系统，支持：
- A/B 分区切换
- UART 通信协议
- CRC32 固件校验
- OLED 状态显示
- PC 上位机工具

### 1.2 技术指标

| 指标 | 目标值 | 实际值 |
|------|--------|--------|
| 传输速度 | >500 字节/秒 | ~580 字节/秒 |
| 12KB 固件耗时 | <30 秒 | ~21 秒 |
| CRC 校验 | 100% | 100% |
| 可靠性 | >99% | 100% |

---

## 2. 系统架构

### 2.1 硬件架构

```
┌─────────────────────────────────────────────────────────┐
│                    STM32F407VETx                        │
├─────────────────────────────────────────────────────────┤
│  Flash (512KB)                                          │
│  ├─ Sector 0-1 (32KB): Bootloader                      │
│  ├─ Sector 2 (16KB): 控制数据                          │
│  ├─ Sector 3-5 (208KB): Partition A                    │
│  └─ Sector 6-7 (256KB): Partition B                    │
├─────────────────────────────────────────────────────────┤
│  外设                                                   │
│  ├─ USART1: 串口通信 (PA9/PA10)                        │
│  ├─ I2C1: OLED 显示 (PB6/PB7)                         │
│  ├─ GPIO: LED (PB2), Boot 按键 (PA0)                  │
│  └─ IWDG: 看门狗保护                                   │
└─────────────────────────────────────────────────────────┘
```

### 2.2 软件架构

```
┌─────────────────────────────────────────────────────────┐
│                    PC 上位机工具                         │
│  ├─ 串口通信模块                                       │
│  ├─ HEX 文件解析                                       │
│  ├─ OTA 协议实现                                       │
│  └─ GUI 界面                                           │
└─────────────────────────────────────────────────────────┘
                           │
                           │ UART (115200 baud)
                           ▼
┌─────────────────────────────────────────────────────────┐
│                    Bootloader                           │
│  ├─ 命令解析模块                                       │
│  ├─ Flash 操作模块                                     │
│  ├─ CRC 校验模块                                       │
│  ├─ OLED 显示模块                                      │
│  └─ 状态管理模块                                       │
└─────────────────────────────────────────────────────────┘
                           │
                           │ Flash 操作
                           ▼
┌─────────────────────────────────────────────────────────┐
│                    Flash 存储                           │
│  ├─ 控制数据 (Sector 2)                                │
│  └─ 固件数据 (Partition A/B)                           │
└─────────────────────────────────────────────────────────┘
```

---

## 3. 通信协议

### 3.1 命令格式

```
命令格式: <命令> [参数]\r\n
响应格式: <响应>\r\n
```

### 3.2 命令列表

| 命令 | 参数 | 响应 | 说明 |
|------|------|------|------|
| `version` | 无 | 版本信息 | 查询版本 |
| `info` | 无 | 分区信息 | 查询分区 |
| `ota_enter` | 无 | OK/ERROR | 进入 Boot 模式 |
| `ota_start <size>` | 固件大小 | OK/ERROR | 开始 OTA |
| `ota_data <hex>` | HEX 数据 | Progress | 发送数据 |
| `ota_end <crc>` | CRC32 | OK/ERROR | 结束 OTA |
| `ota_status` | 无 | 状态信息 | 查询状态 |
| `reset` | 无 | OK | 复位设备 |
| `led_on` | 无 | OK | LED 开启 |
| `led_off` | 无 | OK | LED 关闭 |

### 3.3 OTA 流程

```
1. 发送 ota_enter → 设置 Boot 魔数 → 复位
2. Bootloader 检测魔数 → 进入 OTA 模式
3. 发送 ota_start <size> → 擦除目标分区
4. 发送 ota_data × N → 写入固件数据
5. 发送 ota_end <crc> → CRC 校验 → 切换分区
6. 跳转到新 APP
```

---

## 4. 数据结构

### 4.1 控制数据 (Sector 2)

```c
#define BOOT_CONTROL_MAGIC_ADDR 0x0800BFF0  // Boot 魔数
#define APP_ACTIVE_ADDR         0x0800BFEC  // 活动分区地址
#define APP_CRC_ADDR            0x0800BFE8  // APP CRC
#define APP_SIZE_ADDR           0x0800BFD8  // 固件大小
#define ROLLBACK_FLAG_ADDR      0x0800BFE0  // 回滚标志
```

### 4.2 Bootloader 状态

```c
typedef enum {
    BOOT_STATE_IDLE = 0,      // 空闲
    BOOT_STATE_RECEIVING,     // 接收中
    BOOT_STATE_VERIFYING,     // 校验中
    BOOT_STATE_JUMPING,       // 跳转中
    BOOT_STATE_ROLLBACK,      // 回滚中
    BOOT_STATE_ERROR          // 错误
} BootState_t;
```

---

## 5. 关键设计

### 5.1 UART 中断接收

```c
// 4096 字节环形缓冲区
#define UART_RX_BUF_SIZE 4096
static volatile uint8_t uart_rx_buf[UART_RX_BUF_SIZE];
static volatile uint16_t uart_rx_head = 0;
static volatile uint16_t uart_rx_tail = 0;

// 中断回调
void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart) {
    uint16_t next_head = (uart_rx_head + 1) % UART_RX_BUF_SIZE;
    if (next_head != uart_rx_tail) {
        uart_rx_buf[uart_rx_head] = uart_rx_byte;
        uart_rx_head = next_head;
    }
    HAL_UART_Receive_IT(&huart1, &uart_rx_byte, 1);
}
```

### 5.2 Flash 操作

```c
// 擦除 Sector
HAL_FLASH_Unlock();
FLASH_EraseInitTypeDef erase_init;
erase_init.TypeErase = FLASH_TYPEERASE_SECTORS;
erase_init.Sector = FLASH_SECTOR_3;
erase_init.NbSectors = 3;
HAL_FLASHEx_Erase(&erase_init, &sector_error);
HAL_FLASH_Lock();

// 写入数据
HAL_FLASH_Unlock();
for (uint32_t i = 0; i < len; i += 4) {
    uint32_t word = 0;
    word |= data[i] | (data[i+1] << 8) | (data[i+2] << 16) | (data[i+3] << 24);
    HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD, addr + i, word);
}
HAL_FLASH_Lock();
```

### 5.3 CRC32 校验

```c
uint32_t Bootloader_CalculateCRC(const uint8_t *data, uint32_t len) {
    uint32_t crc = 0xFFFFFFFF;
    for (uint32_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (int j = 0; j < 8; j++) {
            if (crc & 1) {
                crc = (crc >> 1) ^ 0xEDB88320;
            } else {
                crc >>= 1;
            }
        }
    }
    return crc ^ 0xFFFFFFFF;
}
```

---

## 6. PC 上位机工具

### 6.1 功能

- 串口连接/断开
- HEX 文件加载
- OTA 升级（带进度条）
- 设备信息查询
- LED 测试
- 日志记录

### 6.2 使用方法

```bash
# 安装依赖
pip install pyserial

# 运行最简版本
python tools/ota_tool_simple.py

# 运行 GUI 版本
python tools/ota_tool_tkinter.py
```

### 6.3 性能

| 参数 | 值 |
|------|-----|
| 波特率 | 115200 |
| 块大小 | 128 字节 |
| 延时 | 0.2 秒 |
| 速度 | ~580 字节/秒 |

---

## 7. 测试结果

### 7.1 功能测试

| 测试项 | 结果 |
|--------|------|
| UART 通信 | ✅ 正常 |
| Flash 擦写 | ✅ 正常 |
| CRC 校验 | ✅ 正确 |
| OTA 升级 | ✅ 成功 |
| OLED 显示 | ✅ 正常 |
| LED 控制 | ✅ 正常 |
| 按键触发 | ✅ 正常 |

### 7.2 性能测试

| 测试项 | 结果 |
|--------|------|
| 传输速度 | 580 字节/秒 |
| 12KB 耗时 | 21.3 秒 |
| CRC 校验 | 100% 正确 |
| 多次升级 | 100% 成功 |

---

## 8. 优化建议

### 8.1 短期优化

- [ ] 提高波特率（230400/460800）
- [ ] 二进制传输协议
- [ ] 更大的块大小
- [ ] 流控机制

### 8.2 长期优化

- [ ] 双 Bootloader 架构
- [ ] 加密固件传输
- [ ] 签名验证
- [ ] 远程升级（WiFi/4G）

---

## 9. 文件清单

```
stm32-ota-bootloader/
├── Core/
│   ├── Inc/
│   │   ├── bootloader.h
│   │   ├── display_ota.h
│   │   └── main.h
│   └── Src/
│       ├── bootloader.c
│       ├── display_ota.c
│       └── main.c
├── tools/
│   ├── ota_tool_simple.py
│   ├── ota_tool_tkinter.py
│   └── ota_tool_final.py
├── docs/
│   ├── design.md
│   └── bootloader_guide.md
├── tests/
│   └── ota_test.py
├── MDK-ARM/
│   └── test1.uvprojx
└── README.md
```

---

## 10. 总结

本项目实现了完整的 STM32 OTA 升级系统：

1. **Bootloader**：支持 A/B 分区、CRC 校验、OLED 显示
2. **通信协议**：简单可靠的 UART 命令协议
3. **PC 工具**：图形化界面，操作简便
4. **性能达标**：580 字节/秒，12KB 固件 21 秒完成

系统已通过全面测试，可投入生产使用。

---

**文档版本:** v1.0.0
**最后更新:** 2026-07-23
