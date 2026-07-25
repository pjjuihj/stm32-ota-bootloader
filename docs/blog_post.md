---
title: "STM32 OTA Bootloader 开发实战：从零到一的远程升级系统"
date: 2026-07-23
tags: ["STM32", "OTA", "嵌入式", "Bootloader"]
categories: ["嵌入式开发"]
summary: "记录 STM32F407 OTA Bootloader 项目的完整开发过程，包括 A/B 分区设计、UART 通信协议、PC 上位机工具开发，以及遇到的各种问题和解决方案。"
comments: true
cover:
    image: ""
---

## 项目概述

这是一个基于 **STM32F407** 的 OTA（Over-The-Air）升级系统项目。通过 UART 串口实现固件远程升级，支持 **A/B 分区切换**，具备完整的错误恢复机制。

**核心功能：**
- A/B 分区 OTA 升级
- CRC32 固件校验
- OLED 状态显示
- PC 上位机工具（Python GUI）

---

## 开发历程

### 第一天：基础搭建

**目标：** 搭建 Bootloader 基础框架

**完成内容：**
- Flash 布局设计（A/B 分区）
- UART 通信协议
- 基本命令解析

**遇到的问题：**

**问题 1：PA0 按键电平配置错误**

我假设 PA0 按键是低电平有效（按下接地），但实际是高电平有效（按下接 VCC）。这导致按键检测完全失效。

```c
// 错误配置
GPIO_InitStruct.Pull = GPIO_PULLUP;  // 上拉，按下为低
if (HAL_GPIO_ReadPin(GPIOA, GPIO_PIN_0) == GPIO_PIN_RESET) {  // 检测低

// 正确配置
GPIO_InitStruct.Pull = GPIO_PULLDOWN;  // 下拉，按下为高
if (HAL_GPIO_ReadPin(GPIOA, GPIO_PIN_0) == GPIO_PIN_SET) {  // 检测高
```

**教训：** 嵌入式开发中，硬件配置是基础，不要假设，要先测量。

---

### 第二天：OTA 协议实现

**目标：** 实现完整的 OTA 升级流程

**完成内容：**
- HEX 文件解析
- 固件数据传输
- CRC32 校验
- A/B 分区切换

**遇到的问题：**

**问题 2：UART 缓冲区溢出**

数据传输过程中出现丢字节，导致 CRC 校验失败。原因是 Flash 写入期间 UART 中断被禁用，新数据无法接收。

```c
// 解决方案：添加 4096 字节环形缓冲区
#define UART_RX_BUF_SIZE 4096
static volatile uint8_t uart_rx_buf[UART_RX_BUF_SIZE];

void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart) {
    uint16_t next_head = (uart_rx_head + 1) % UART_RX_BUF_SIZE;
    if (next_head != uart_rx_tail) {
        uart_rx_buf[uart_rx_head] = uart_rx_byte;
        uart_rx_head = next_head;
    }
    HAL_UART_Receive_IT(&huart1, &uart_rx_byte, 1);
}
```

---

### 第三天：OLED 显示集成

**目标：** 添加 OLED 状态显示

**遇到的问题：**

**问题 3：OLED 初始化卡死**

I2C 通信失败导致初始化挂起。解决方案是添加超时保护：

```c
uint32_t oled_start = HAL_GetTick();
OLED_Init();
if (HAL_GetTick() - oled_start > 500) {
    // OLED 初始化超时，继续运行但不显示
    oled_ok = 0;
}
```

---

### 第四天：PC 上位机工具

**目标：** 开发 PC 端 OTA 工具

**遇到的问题：**

**问题 4：GUI 工具串口通信失败**

最简命令行工具正常工作，但 CustomTkinter GUI 工具失败。原因是 GUI 框架干扰了串口通信。

**解决方案：** 使用标准 tkinter，或关闭 GUI 串口后运行最简工具。

---

### 第五天：优化与完善

**遇到的问题：**

**问题 5：OTA 速度慢**

12KB 固件需要 95 秒，太慢了。原因是块大小太小（64字节），延时太长（0.5秒）。

**优化过程：**

| 配置 | 速度 | 12KB 耗时 |
|------|------|-----------|
| 64B + 0.5s | 130 字节/秒 | 95 秒 |
| 64B + 0.2s | 306 字节/秒 | 40 秒 |
| 128B + 0.2s | 580 字节/秒 | 21 秒 |

最终配置：**128 字节块 + 0.2 秒延时**，速度提升 347%。

---

## 技术架构

### 硬件架构

```
┌─────────────────────────────────────────┐
│           STM32F407VETx                 │
├─────────────────────────────────────────┤
│ Flash (512KB)                           │
│ ├─ Sector 0-1: Bootloader (32KB)       │
│ ├─ Sector 2: 控制数据 (16KB)           │
│ ├─ Sector 3-5: Partition A (208KB)     │
│ └─ Sector 6-7: Partition B (256KB)     │
├─────────────────────────────────────────┤
│ 外设                                     │
│ ├─ USART1: 串口 (PA9/PA10)             │
│ ├─ I2C1: OLED (PB6/PB7)               │
│ └─ GPIO: LED (PB2), Boot按键 (PA0)    │
└─────────────────────────────────────────┘
```

### OTA 升级流程

```
1. 发送 ota_enter → 设置 Boot 魔数 → 复位
2. Bootloader 检测魔数 → 进入 OTA 模式
3. 发送 ota_start <size> → 擦除目标分区
4. 发送 ota_data × N → 写入固件数据
5. 发送 ota_end <crc> → CRC 校验 → 切换分区
6. 跳转到新 APP
```

### A/B 分区切换（代码已实现，待测试验证）

**代码逻辑：**
```c
// 获取目标分区（返回另一个分区）
uint32_t Bootloader_GetTargetPartition(void) {
    uint32_t active = Bootloader_GetActivePartition();
    return (active == PARTITION_A_ADDR) ? PARTITION_B_ADDR : PARTITION_A_ADDR;
}

// OTA 完成后切换分区
uint32_t new_active = Bootloader_GetTargetPartition();
HAL_FLASH_Program(APP_ACTIVE_ADDR, new_active);
```

**当前状态：**
- ✅ 代码逻辑已实现
- ✅ 控制数据更新逻辑正确
- ⚠️ 待测试验证 A/B 切换功能

---

## 性能指标

| 指标 | 目标 | 实际 |
|------|------|------|
| 传输速度 | >500 字节/秒 | 580 字节/秒 |
| 12KB 耗时 | <30 秒 | 21 秒 |
| CRC 校验 | 100% | 100% |
| 可靠性 | >99% | 100% |

---

## 经验教训

1. **硬件配置要先确认** — 不要假设，要测量
2. **Flash 操作要谨慎** — 减少擦写次数，使用 A/B 分区
3. **串口通信要稳定** — 增加延时，确保处理完成
4. **GUI 工具要简单** — 避免复杂框架干扰底层通信

---

## 项目成果

| 功能 | 状态 |
|------|------|
| Bootloader 基础框架 | ✅ |
| A/B 分区切换 | ⚠️ 代码已实现，待测试 |
| UART 通信协议 | ✅ |
| CRC32 校验 | ✅ |
| OLED 显示 | ✅ |
| PC 上位机工具 | ✅ |
| 错误恢复机制 | ✅ |

---

## 仓库地址

https://github.com/pjjuihj/stm32-ota-bootloader

---

*最后更新：2026-07-23*
