# Bootloader 分层错误处理系统实施计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 为 STM32F407VETx Bootloader 实现分层错误处理系统，提高可靠性、掉电保护和错误恢复能力

**架构:** 五层架构：硬件层 → 日志层 → 协议层 → 显示层 → 应用层，每层独立封装，逐层集成测试

**Tech Stack:** STM32F4 HAL 库, C语言, Keil MDK-ARM

## Global Constraints

- 目标芯片: STM32F407VETx (512KB Flash, 192KB RAM)
- Flash 分区: Bootloader 32KB, Sector 2 控制数据 16KB, Partition A 208KB, Partition B 256KB
- 编译器: Keil MDK-ARM
- 调试工具: 串口终端 (115200 baud)
- 测试方法: 每完成一个模块烧录测试，验证功能正常后再进入下一个模块
- LED 指示: PB2 (低电平点亮)
- OLED: I2C1 (PB6-SCL, PB7-SDA)

---

## 文件结构

### 新增文件

| 文件路径 | 职责 |
|---------|------|
| `Core/Inc/flash_driver.h` | Flash 操作封装接口 |
| `Core/Src/flash_driver.c` | Flash 重试逻辑实现 |
| `Core/Inc/error_log.h` | 错误日志系统接口 |
| `Core/Src/error_log.c` | 错误日志实现 |
| `Core/Inc/protocol.h` | 通信协议接口 |
| `Core/Src/protocol.c` | 通信协议实现 |
| `Core/Inc/oled_wrapper.h` | OLED 封装接口 |
| `Core/Src/oled_wrapper.c` | OLED 错误处理实现 |
| `Core/Inc/led指示.h` | LED 指示增强接口 |
| `Core/Src/led指示.c` | LED 指示实现 |

### 修改文件

| 文件路径 | 改动内容 |
|---------|---------|
| `Core/Inc/bootloader.h` | 新增错误等级、OTA 状态、恢复策略定义 |
| `Core/Src/bootloader.c` | 重构错误处理、集成新模块 |
| `Core/Inc/display_ota.h` | 新增显示错误类型定义 |
| `Core/Src/display_ota.c` | 集成 OLED 封装 |
| `Core/Src/main.c` | 集成新模块、启动时恢复状态 |

---

## Task 1: Flash 驱动封装 (硬件层)

**目标:** 封装 Flash 操作，增加自动重试和错误检测

**依赖:** 无

**测试验证:** 串口输出 Flash 擦写结果，验证重试机制工作

---

### Task 1.1: 创建 flash_driver.h

**Files:**
- Create: `Core/Inc/flash_driver.h`

- [ ] **Step 1: 编写 flash_driver.h 头文件**

```c
/**
  ******************************************************************************
  * @file           : flash_driver.h
  * @brief          : Flash 操作封装接口
  ******************************************************************************
  * @attention
  *  封装 HAL Flash 操作，增加自动重试和错误检测
  ******************************************************************************
  */

#ifndef __FLASH_DRIVER_H
#define __FLASH_DRIVER_H

#ifdef __cplusplus
extern "C" {
#endif

#include "stm32f4xx_hal.h"
#include <stdint.h>
#include <stdbool.h>

/* 错误严重等级 */
typedef enum {
    FLASH_ERR_LVL_WARN = 0,     // 警告，可以继续
    FLASH_ERR_LVL_ERROR = 1,    // 错误，需要重试
    FLASH_ERR_LVL_FATAL = 2     // 致命错误，必须停止
} FlashErrorLevel_t;

/* Flash 操作结果 */
typedef struct {
    HAL_StatusTypeDef status;   // HAL 返回值
    uint32_t retries;           // 重试次数
    FlashErrorLevel_t level;    // 错误等级
    const char *msg;            // 错误描述
} FlashResult_t;

/* Flash 操作配置 */
#define FLASH_MAX_RETRIES       3       // 最大重试次数
#define FLASH_RETRY_DELAY_MS    10      // 重试间隔 (ms)

/* 函数声明 */

/**
  * @brief  初始化 Flash 驱动
  * @retval None
  */
void FlashDriver_Init(void);

/**
  * @brief  擦除扇区 (带重试)
  * @param  sector: 扇区号 (FLASH_SECTOR_0 到 FLASH_SECTOR_7)
  * @param  nb_sectors: 扇区数量
  * @retval FlashResult_t 操作结果
  */
FlashResult_t FlashDriver_EraseSector(uint32_t sector, uint32_t nb_sectors);

/**
  * @brief  写入一个字 (带重试和回读校验)
  * @param  addr: Flash 地址 (必须 4 字节对齐)
  * @param  data: 要写入的数据
  * @retval FlashResult_t 操作结果
  */
FlashResult_t FlashDriver_WriteWord(uint32_t addr, uint32_t data);

/**
  * @brief  写入数据块 (带重试)
  * @param  addr: Flash 起始地址
  * @param  data: 数据指针
  * @param  len: 数据长度 (字节)
  * @retval FlashResult_t 操作结果
  */
FlashResult_t FlashDriver_WriteBlock(uint32_t addr, const uint8_t *data, uint32_t len);

/**
  * @brief  读取一个字
  * @param  addr: Flash 地址
  * @retval 读取的数据
  */
uint32_t FlashDriver_ReadWord(uint32_t addr);

#ifdef __cplusplus
}
#endif

#endif /* __FLASH_DRIVER_H */
```

- [ ] **Step 2: 烧录验证头文件编译通过**

烧录到开发板，确认编译无错误

---

### Task 1.2: 实现 flash_driver.c

**Files:**
- Create: `Core/Src/flash_driver.c`

- [ ] **Step 1: 编写 flash_driver.c 实现**

```c
/**
  ******************************************************************************
  * @file           : flash_driver.c
  * @brief          : Flash 操作封装实现
  ******************************************************************************
  */

#include "flash_driver.h"
#include "bootloader.h"
#include <stdio.h>
#include <string.h>

/* 外部变量 */
extern UART_HandleTypeDef huart1;

/* 私有函数声明 */
static void FlashDriver_SendDebug(const char *msg);
static FlashResult_t FlashDriver_Operate(FlashOperation op, uint32_t addr, uint32_t data);

/* 初始化 */
void FlashDriver_Init(void)
{
    FlashDriver_SendDebug("FlashDriver: Initialized");
}

/* 扇区擦除 (带重试) */
FlashResult_t FlashDriver_EraseSector(uint32_t sector, uint32_t nb_sectors)
{
    FlashResult_t result = {0};
    FLASH_EraseInitTypeDef erase_init;
    uint32_t sector_error = 0;

    erase_init.TypeErase = FLASH_TYPEERASE_SECTORS;
    erase_init.Sector = sector;
    erase_init.NbSectors = nb_sectors;
    erase_init.VoltageRange = FLASH_VOLTAGE_RANGE_3;

    for (uint32_t retry = 0; retry <= FLASH_MAX_RETRIES; retry++) {
        __disable_irq();
        HAL_FLASH_Unlock();

        result.status = HAL_FLASHEx_Erase(&erase_init, &sector_error);
        result.retries = retry;

        HAL_FLASH_Lock();
        __enable_irq();

        if (result.status == HAL_OK) {
            result.level = FLASH_ERR_LVL_WARN;
            result.msg = "Erase OK";
            char msg[64];
            snprintf(msg, sizeof(msg), "Flash: Erased sector %lu OK", sector);
            FlashDriver_SendDebug(msg);
            return result;
        }

        if (retry < FLASH_MAX_RETRIES) {
            result.level = FLASH_ERR_LVL_ERROR;
            result.msg = "Erase retry";
            char msg[64];
            snprintf(msg, sizeof(msg), "Flash: Erase failed, retry %lu/%d", 
                     retry + 1, FLASH_MAX_RETRIES);
            FlashDriver_SendDebug(msg);
            HAL_Delay(FLASH_RETRY_DELAY_MS);
        }
    }

    result.level = FLASH_ERR_LVL_FATAL;
    result.msg = "Erase failed after retries";
    FlashDriver_SendDebug("Flash: FATAL - Erase failed after max retries");
    return result;
}

/* 写入一个字 (带重试和回读校验) */
FlashResult_t FlashDriver_WriteWord(uint32_t addr, uint32_t data)
{
    FlashResult_t result = {0};

    for (uint32_t retry = 0; retry <= FLASH_MAX_RETRIES; retry++) {
        __disable_irq();
        HAL_FLASH_Unlock();

        result.status = HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD, addr, data);
        result.retries = retry;

        /* 回读校验 */
        uint32_t readback = *(volatile uint32_t *)addr;
        HAL_FLASH_Lock();
        __enable_irq();

        if (result.status == HAL_OK && readback == data) {
            result.level = FLASH_ERR_LVL_WARN;
            result.msg = "Write OK";
            return result;
        }

        if (retry < FLASH_MAX_RETRIES) {
            result.level = FLASH_ERR_LVL_ERROR;
            result.msg = "Write retry";
            char msg[128];
            snprintf(msg, sizeof(msg), "Flash: Write 0x%08lX failed, retry %lu/%d",
                     addr, retry + 1, FLASH_MAX_RETRIES);
            FlashDriver_SendDebug(msg);
            HAL_Delay(FLASH_RETRY_DELAY_MS);
        }
    }

    result.level = FLASH_ERR_LVL_FATAL;
    result.msg = "Write failed after retries";
    char msg[64];
    snprintf(msg, sizeof(msg), "Flash: FATAL - Write 0x%08lX failed", addr);
    FlashDriver_SendDebug(msg);
    return result;
}

/* 写入数据块 */
FlashResult_t FlashDriver_WriteBlock(uint32_t addr, const uint8_t *data, uint32_t len)
{
    FlashResult_t result = {0};

    for (uint32_t i = 0; i < len; i += 4) {
        uint32_t word = 0;
        for (int j = 0; j < 4 && (i + j) < len; j++) {
            word |= ((uint32_t)data[i + j] << (j * 8));
        }

        result = FlashDriver_WriteWord(addr + i, word);
        if (result.status != HAL_OK) {
            return result;
        }
    }

    result.level = FLASH_ERR_LVL_WARN;
    result.msg = "Block write OK";
    return result;
}

/* 读取一个字 */
uint32_t FlashDriver_ReadWord(uint32_t addr)
{
    return *(volatile uint32_t *)addr;
}

/* 调试输出 */
static void FlashDriver_SendDebug(const char *msg)
{
    HAL_UART_Transmit(&huart1, (uint8_t *)msg, strlen(msg), 100);
    HAL_UART_Transmit(&huart1, (uint8_t *)"\r\n", 2, 100);
}
```

- [ ] **Step 2: 烧录测试 Flash 擦写功能**

烧录后通过串口发送以下命令测试：
```
test
```
预期输出：Flash 擦写成功信息

- [ ] **Step 3: 提交代码**

```bash
git add Core/Inc/flash_driver.h Core/Src/flash_driver.c
git commit -m "feat: add flash driver wrapper with retry mechanism"
```

---

## Task 2: 错误日志系统 (日志层)

**目标:** 实现 RAM 缓冲 + 批量写入的错误日志系统

**依赖:** Task 1 (Flash 驱动)

**测试验证:** 串口输出日志记录，验证 RAM 缓冲和批量刷写

---

### Task 2.1: 创建 error_log.h

**Files:**
- Create: `Core/Inc/error_log.h`

- [ ] **Step 1: 编写 error_log.h 头文件**

```c
/**
  ******************************************************************************
  * @file           : error_log.h
  * @brief          : 错误日志系统接口
  ******************************************************************************
  */

#ifndef __ERROR_LOG_H
#define __ERROR_LOG_H

#ifdef __cplusplus
extern "C" {
#endif

#include "stm32f4xx_hal.h"
#include <stdint.h>
#include <stdbool.h>

/* 错误严重等级 */
typedef enum {
    ERROR_SEVERITY_WARN = 0,        // 警告
    ERROR_SEVERITY_ERROR = 1,       // 错误
    ERROR_SEVERITY_FATAL = 2        // 致命错误
} ErrorSeverity_t;

/* 错误代码 (扩展) */
typedef enum {
    ERROR_LOG_NONE = 0,
    ERROR_LOG_FLASH_ERASE,
    ERROR_LOG_FLASH_WRITE,
    ERROR_LOG_CRC_MISMATCH,
    ERROR_LOG_INVALID_APP,
    ERROR_LOG_UART_TIMEOUT,
    ERROR_LOG_UART_OVERFLOW,
    ERROR_LOG_I2C_TIMEOUT,          // 新增
    ERROR_LOG_I2C_NACK,             // 新增
    ERROR_LOG_OLED_FAIL,            // 新增
    ERROR_LOG_PROTOCOL_NACK,        // 新增
    ERROR_LOG_PROTOCOL_CRC,         // 新增
    ERROR_LOG_POWER_LOSS,           // 新增
    ERROR_LOG_UNKNOWN
} ErrorCode_t;

/* 错误日志条目 */
typedef struct {
    uint32_t error_code;            // 错误代码
    uint32_t timestamp;             // 错误时间 (HAL_GetTick)
    uint32_t state;                 // 发生时的 OTA 状态
    uint8_t severity;               // 严重等级 (ErrorSeverity_t)
    uint8_t retry_count;            // 已重试次数
    uint16_t line;                  // 发生位置 (可选)
} ErrorLogEntry_t;

/* 错误日志配置 */
#define ERROR_LOG_RAM_SIZE          16      // RAM 缓冲大小
#define ERROR_LOG_FLASH_SIZE        8       // Flash 保留条目数
#define ERROR_LOG_FLUSH_THRESHOLD   5       // 触发刷写的阈值

/* 错误日志管理器 */
typedef struct {
    ErrorLogEntry_t ram_buffer[ERROR_LOG_RAM_SIZE];
    uint8_t ram_count;
    uint8_t flash_count;
    uint32_t total_errors;
    bool needs_flush;
} ErrorLogManager_t;

/* 函数声明 */

/**
  * @brief  初始化错误日志系统
  * @retval None
  */
void ErrorLog_Init(void);

/**
  * @brief  记录错误 (写入 RAM 缓冲)
  * @param  error_code: 错误代码
  * @param  severity: 严重等级
  * @param  line: 发生位置 (可选，传 0 忽略)
  * @retval None
  */
void ErrorLog_Add(ErrorCode_t error_code, ErrorSeverity_t severity, uint16_t line);

/**
  * @brief  批量刷写到 Flash
  * @retval true=成功, false=失败
  */
bool ErrorLog_Flush(void);

/**
  * @brief  读取最近 N 条错误
  * @param  entries: 输出缓冲区
  * @param  max_count: 最大读取条数
  * @retval 实际读取的条数
  */
uint32_t ErrorLog_Read(ErrorLogEntry_t *entries, uint32_t max_count);

/**
  * @brief  清空所有错误日志
  * @retval true=成功, false=失败
  */
bool ErrorLog_Clear(void);

/**
  * @brief  获取错误统计
  * @param  total: 总错误数输出
  * @param  fatal_count: FATAL 错误数输出
  * @retval None
  */
void ErrorLog_GetStats(uint32_t *total, uint32_t *fatal_count);

/**
  * @brief  检查是否需要刷写
  * @retval true=需要刷写, false=不需要
  */
bool ErrorLog_NeedsFlush(void);

#ifdef __cplusplus
}
#endif

#endif /* __ERROR_LOG_H */
```

- [ ] **Step 2: 烧录验证头文件编译通过**

---

### Task 2.2: 实现 error_log.c

**Files:**
- Create: `Core/Src/error_log.c`

- [ ] **Step 1: 编写 error_log.c 实现**

```c
/**
  ******************************************************************************
  * @file           : error_log.c
  * @brief          : 错误日志系统实现
  ******************************************************************************
  */

#include "error_log.h"
#include "flash_driver.h"
#include <stdio.h>
#include <string.h>

/* 外部变量 */
extern UART_HandleTypeDef huart1;

/* Flash 地址定义 (Sector 2) */
#define ERROR_LOG_ADDR          0x0800BF80U
#define ERROR_LOG_COUNT_ADDR    0x0800BF80U
#define ERROR_LOG_DATA_ADDR     0x0800BF84U

/* 静态变量 */
static ErrorLogManager_t error_log;

/* 私有函数声明 */
static void ErrorLog_SendDebug(const char *msg);
static bool ErrorLog_WriteFlash(void);

/* 初始化 */
void ErrorLog_Init(void)
{
    memset(&error_log, 0, sizeof(ErrorLogManager_t));
    
    /* 从 Flash 读取现有错误计数 */
    uint32_t count = FlashDriver_ReadWord(ERROR_LOG_COUNT_ADDR);
    if (count != 0xFFFFFFFF) {
        error_log.flash_count = (count > 255) ? 255 : count;
    }
    
    ErrorLog_SendDebug("ErrorLog: Initialized");
}

/* 记录错误 */
void ErrorLog_Add(ErrorCode_t error_code, ErrorSeverity_t severity, uint16_t line)
{
    /* 写入 RAM 缓冲 */
    if (error_log.ram_count < ERROR_LOG_RAM_SIZE) {
        ErrorLogEntry_t *entry = &error_log.ram_buffer[error_log.ram_count];
        entry->error_code = error_code;
        entry->timestamp = HAL_GetTick();
        entry->severity = severity;
        entry->retry_count = 0;
        entry->line = line;
        error_log.ram_count++;
    } else {
        /* RAM 满，强制刷写 */
        ErrorLog_Flush();
        if (error_log.ram_count < ERROR_LOG_RAM_SIZE) {
            ErrorLogEntry_t *entry = &error_log.ram_buffer[error_log.ram_count];
            entry->error_code = error_code;
            entry->timestamp = HAL_GetTick();
            entry->severity = severity;
            entry->retry_count = 0;
            entry->line = line;
            error_log.ram_count++;
        }
    }
    
    error_log.total_errors++;
    
    /* FATAL 错误立即刷写 */
    if (severity == ERROR_SEVERITY_FATAL) {
        ErrorLog_Flush();
    }
    /* 或者累积到阈值 */
    else if (error_log.ram_count >= ERROR_LOG_FLUSH_THRESHOLD) {
        error_log.needs_flush = true;
    }
    
    /* 串口输出 */
    char msg[64];
    snprintf(msg, sizeof(msg), "ErrorLog: [%d] code=%d line=%d",
             severity, error_code, line);
    ErrorLog_SendDebug(msg);
}

/* 批量刷写到 Flash */
bool ErrorLog_Flush(void)
{
    if (error_log.ram_count == 0) {
        return true;
    }
    
    bool result = ErrorLog_WriteFlash();
    if (result) {
        error_log.flash_count += error_log.ram_count;
        error_log.ram_count = 0;
        error_log.needs_flush = false;
        ErrorLog_SendDebug("ErrorLog: Flush OK");
    } else {
        ErrorLog_SendDebug("ErrorLog: Flush FAILED");
    }
    
    return result;
}

/* 读取最近 N 条错误 */
uint32_t ErrorLog_Read(ErrorLogEntry_t *entries, uint32_t max_count)
{
    uint32_t count = 0;
    
    /* 先读 RAM 中的 */
    uint32_t ram_start = (error_log.ram_count > max_count) ? 
                         (error_log.ram_count - max_count) : 0;
    uint32_t ram_count = error_log.ram_count - ram_start;
    
    for (uint32_t i = 0; i < ram_count && count < max_count; i++) {
        entries[count++] = error_log.ram_buffer[ram_start + i];
    }
    
    return count;
}

/* 清空所有错误日志 */
bool ErrorLog_Clear(void)
{
    error_log.ram_count = 0;
    error_log.total_errors = 0;
    error_log.needs_flush = false;
    
    /* 擦除 Sector 2 中的日志区域 (需要重写其他控制数据) */
    /* 这里简化处理，实际应该保留其他控制数据 */
    ErrorLog_SendDebug("ErrorLog: Cleared");
    return true;
}

/* 获取错误统计 */
void ErrorLog_GetStats(uint32_t *total, uint32_t *fatal_count)
{
    *total = error_log.total_errors;
    
    uint32_t fatal = 0;
    for (uint32_t i = 0; i < error_log.ram_count; i++) {
        if (error_log.ram_buffer[i].severity == ERROR_SEVERITY_FATAL) {
            fatal++;
        }
    }
    *fatal_count = fatal;
}

/* 检查是否需要刷写 */
bool ErrorLog_NeedsFlush(void)
{
    return error_log.needs_flush;
}

/* 内部：写入 Flash */
static bool ErrorLog_WriteFlash(void)
{
    /* 使用 Flash 驱动写入 */
    FlashResult_t result;
    
    /* 写入错误计数 */
    uint32_t new_count = error_log.flash_count + error_log.ram_count;
    result = FlashDriver_WriteWord(ERROR_LOG_COUNT_ADDR, new_count);
    if (result.status != HAL_OK) {
        return false;
    }
    
    /* 写入 RAM 中的记录 */
    uint32_t log_addr = ERROR_LOG_DATA_ADDR;
    for (uint8_t i = 0; i < error_log.ram_count; i++) {
        result = FlashDriver_WriteBlock(log_addr + (i * sizeof(ErrorLogEntry_t)),
                                        (uint8_t *)&error_log.ram_buffer[i],
                                        sizeof(ErrorLogEntry_t));
        if (result.status != HAL_OK) {
            return false;
        }
    }
    
    return true;
}

/* 调试输出 */
static void ErrorLog_SendDebug(const char *msg)
{
    HAL_UART_Transmit(&huart1, (uint8_t *)msg, strlen(msg), 100);
    HAL_UART_Transmit(&huart1, (uint8_t *)"\r\n", 2, 100);
}
```

- [ ] **Step 2: 烧录测试错误日志功能**

烧录后通过串口发送以下命令测试：
```
test
```
预期输出：ErrorLog 初始化成功，错误记录正常

- [ ] **Step 3: 提交代码**

```bash
git add Core/Inc/error_log.h Core/Src/error_log.c
git commit -m "feat: add error log system with RAM buffer and batch flush"
```

---

## Task 3: 通信协议增强 (协议层)

**目标:** 增加 ACK/NACK 握手和重传机制

**依赖:** Task 1 (Flash 驱动)

**测试验证:** 串口发送测试命令，验证 ACK/NACK 握手

---

### Task 3.1: 创建 protocol.h

**Files:**
- Create: `Core/Inc/protocol.h`

- [ ] **Step 1: 编写 protocol.h 头文件**

```c
/**
  ******************************************************************************
  * @file           : protocol.h
  * @brief          : 通信协议接口
  ******************************************************************************
  */

#ifndef __PROTOCOL_H
#define __PROTOCOL_H

#ifdef __cplusplus
extern "C" {
#endif

#include "stm32f4xx_hal.h"
#include <stdint.h>
#include <stdbool.h>

/* 协议命令 */
typedef enum {
    PROTO_CMD_OTA_ENTER = 0,
    PROTO_CMD_OTA_START,
    PROTO_CMD_OTA_DATA,
    PROTO_CMD_OTA_END,
    PROTO_CMD_OTA_STATUS,
    PROTO_CMD_RESET,
    PROTO_CMD_VERSION,
    PROTO_CMD_ACK,             // 确认
    PROTO_CMD_NACK,            // 否认，请求重传
    PROTO_CMD_PING             // 心跳检测
} ProtocolCmd_t;

/* NACK 原因 */
typedef enum {
    PROTO_NACK_NONE = 0,
    PROTO_NACK_CRC_ERROR,      // CRC 校验失败
    PROTO_NACK_SEQ_ERROR,      // 序列号错误
    PROTO_NACK_TIMEOUT,        // 超时
    PROTO_NACK_BUFFER_FULL     // 缓冲区满
} ProtocolNackReason_t;

/* 数据包结构 */
typedef struct __attribute__((packed)) {
    uint8_t seq;                // 序列号 (0-255 循环)
    uint8_t cmd;                // 命令类型 (ProtocolCmd_t)
    uint16_t len;               // 数据长度
    uint32_t crc32;             // 包头+CRC (从 seq 到 data)
    uint8_t data[];             // 数据 payload
} ProtocolPacket_t;

/* 协议配置 */
#define PROTO_MAX_DATA_SIZE     256     // 最大数据长度
#define PROTO_PACKET_HEADER_SIZE 8      // 包头大小 (seq+cmd+len+crc32)
#define PROTO_ACK_TIMEOUT_MS    1000    // ACK 等待超时
#define PROTO_MAX_RETRIES       5       // 最大重试次数

/* 协议状态 */
typedef struct {
    uint8_t expected_seq;               // 期望的序列号
    uint32_t received_size;             // 已接收大小
    uint32_t total_size;                // 总大小
    uint8_t retry_count;                // 当前重试次数
    uint32_t last_activity_tick;        // 最后活动时间
} ProtocolState_t;

/* 函数声明 */

/**
  * @brief  初始化协议模块
  * @retval None
  */
void Protocol_Init(void);

/**
  * @brief  发送 ACK
  * @param  seq: 确认的序列号
  * @retval None
  */
void Protocol_SendACK(uint8_t seq);

/**
  * @brief  发送 NACK
  * @param  seq: 否认的序列号
  * @param  reason: NACK 原因
  * @retval None
  */
void Protocol_SendNACK(uint8_t seq, ProtocolNackReason_t reason);

/**
  * @brief  计算 CRC32
  * @param  data: 数据指针
  * @param  len: 数据长度
  * @retval CRC32 值
  */
uint32_t Protocol_CalcCRC32(const uint8_t *data, uint32_t len);

/**
  * @brief  发送数据包 (带 ACK 等待)
  * @param  cmd: 命令类型
  * @param  data: 数据指针
  * @param  len: 数据长度
  * @retval true=成功, false=失败
  */
bool Protocol_SendPacket(ProtocolCmd_t cmd, const uint8_t *data, uint16_t len);

/**
  * @brief  等待响应 (带超时)
  * @param  timeout_ms: 超时时间
  * @retval 收到的命令类型, 超时返回 -1
  */
int16_t Protocol_WaitResponse(uint32_t timeout_ms);

/**
  * @brief  检查超时
  * @retval true=超时, false=未超时
  */
bool Protocol_CheckTimeout(void);

/**
  * @brief  获取协议状态
  * @retval ProtocolState_t 指针
  */
ProtocolState_t* Protocol_GetState(void);

#ifdef __cplusplus
}
#endif

#endif /* __PROTOCOL_H */
```

- [ ] **Step 2: 烧录验证头文件编译通过**

---

### Task 3.2: 实现 protocol.c

**Files:**
- Create: `Core/Src/protocol.c`

- [ ] **Step 1: 编写 protocol.c 实现**

```c
/**
  ******************************************************************************
  * @file           : protocol.c
  * @brief          : 通信协议实现
  ******************************************************************************
  */

#include "protocol.h"
#include <stdio.h>
#include <string.h>

/* 外部变量 */
extern UART_HandleTypeDef huart1;

/* 静态变量 */
static ProtocolState_t proto_state;

/* 私有函数声明 */
static void Protocol_SendDebug(const char *msg);

/* 初始化 */
void Protocol_Init(void)
{
    memset(&proto_state, 0, sizeof(ProtocolState_t));
    proto_state.expected_seq = 0;
    proto_state.last_activity_tick = HAL_GetTick();
    
    Protocol_SendDebug("Protocol: Initialized");
}

/* 发送 ACK */
void Protocol_SendACK(uint8_t seq)
{
    uint8_t data[1] = {seq};
    Protocol_SendPacket(PROTO_CMD_ACK, data, 1);
}

/* 发送 NACK */
void Protocol_SendNACK(uint8_t seq, ProtocolNackReason_t reason)
{
    uint8_t data[2] = {seq, (uint8_t)reason};
    Protocol_SendPacket(PROTO_CMD_NACK, data, 2);
}

/* 计算 CRC32 */
uint32_t Protocol_CalcCRC32(const uint8_t *data, uint32_t len)
{
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

/* 发送数据包 */
bool Protocol_SendPacket(ProtocolCmd_t cmd, const uint8_t *data, uint16_t len)
{
    if (len > PROTO_MAX_DATA_SIZE) {
        return false;
    }
    
    /* 构造包头 */
    ProtocolPacket_t packet;
    packet.seq = proto_state.expected_seq;
    packet.cmd = (uint8_t)cmd;
    packet.len = len;
    
    /* 计算 CRC (不含 crc32 字段本身) */
    uint8_t header_for_crc[6] = {
        packet.seq,
        packet.cmd,
        (uint8_t)(len & 0xFF),
        (uint8_t)(len >> 8)
    };
    packet.crc32 = Protocol_CalcCRC32(header_for_crc, 6);
    if (data && len > 0) {
        packet.crc32 = Protocol_CalcCRC32(data, len) ^ packet.crc32;
    }
    
    /* 发送包头 */
    HAL_UART_Transmit(&huart1, (uint8_t *)&packet, PROTO_PACKET_HEADER_SIZE, 100);
    
    /* 发送数据 */
    if (data && len > 0) {
        HAL_UART_Transmit(&huart1, (uint8_t *)data, len, 100);
    }
    
    return true;
}

/* 等待响应 */
int16_t Protocol_WaitResponse(uint32_t timeout_ms)
{
    uint32_t start = HAL_GetTick();
    uint8_t buffer[PROTO_PACKET_HEADER_SIZE + PROTO_MAX_DATA_SIZE];
    uint16_t index = 0;
    
    while ((HAL_GetTick() - start) < timeout_ms) {
        /* 尝试接收 */
        uint8_t byte;
        if (HAL_UART_Receive(&huart1, &byte, 1, 10) == HAL_OK) {
            buffer[index++] = byte;
            
            /* 检查是否收到完整包头 */
            if (index >= PROTO_PACKET_HEADER_SIZE) {
                ProtocolPacket_t *pkt = (ProtocolPacket_t *)buffer;
                
                /* 检查数据长度 */
                if (index >= PROTO_PACKET_HEADER_SIZE + pkt->len) {
                    /* 收到完整包，返回命令 */
                    return pkt->cmd;
                }
            }
        }
    }
    
    return -1;  // 超时
}

/* 检查超时 */
bool Protocol_CheckTimeout(void)
{
    return (HAL_GetTick() - proto_state.last_activity_tick) > 30000;  // 30秒
}

/* 获取协议状态 */
ProtocolState_t* Protocol_GetState(void)
{
    return &proto_state;
}

/* 调试输出 */
static void Protocol_SendDebug(const char *msg)
{
    HAL_UART_Transmit(&huart1, (uint8_t *)msg, strlen(msg), 100);
    HAL_UART_Transmit(&huart1, (uint8_t *)"\r\n", 2, 100);
}
```

- [ ] **Step 2: 烧录测试协议功能**

烧录后通过串口发送以下命令测试：
```
test
```
预期输出：Protocol 初始化成功

- [ ] **Step 3: 提交代码**

```bash
git add Core/Inc/protocol.h Core/Src/protocol.c
git commit -m "feat: add communication protocol with ACK/NACK handshake"
```

---

## Task 4: OLED 封装 (显示层)

**目标:** 封装 OLED 操作，增加 I2C 错误处理和降级显示

**依赖:** Task 1 (Flash 驱动)

**测试验证:** OLED 显示正常，模拟 I2C 错误时降级到 LED 指示

---

### Task 4.1: 创建 oled_wrapper.h

**Files:**
- Create: `Core/Inc/oled_wrapper.h`

- [ ] **Step 1: 编写 oled_wrapper.h 头文件**

```c
/**
  ******************************************************************************
  * @file           : oled_wrapper.h
  * @brief          : OLED 封装接口
  ******************************************************************************
  */

#ifndef __OLED_WRAPPER_H
#define __OLED_WRAPPER_H

#ifdef __cplusplus
extern "C" {
#endif

#include "stm32f4xx_hal.h"
#include <stdint.h>
#include <stdbool.h>

/* 显示错误类型 */
typedef enum {
    DISP_ERR_NONE = 0,
    DISP_ERR_I2C_TIMEOUT,
    DISP_ERR_I2C_NACK,
    DISP_ERR_BUFFER_FULL,
    DISP_ERR_INVALID_PARAM
} DisplayError_t;

/* 降级模式 */
typedef enum {
    DEGRADE_NONE = 0,           // 正常显示
    DEGRADE_RETRY,              // 重试 I2C
    DEGRADE_LED_ONLY,           // 仅 LED 指示
    DEGRADE_SILENT              // 静默模式
} DegradeMode_t;

/* OLED 操作结果 */
typedef struct {
    HAL_StatusTypeDef i2c_status;
    bool display_ok;
    uint8_t error_count;
} OLED_Result_t;

/* 错误回调类型 */
typedef void (*DisplayErrorCallback_t)(DisplayError_t error);

/* 配置 */
#define OLED_MAX_RETRIES        3
#define OLED_I2C_TIMEOUT_MS     100
#define OLED_ERROR_THRESHOLD    5       // 连续错误阈值

/* 函数声明 */

/**
  * @brief  初始化 OLED 封装
  * @retval None
  */
void OLED_Wrapper_Init(void);

/**
  * @brief  带错误处理的 OLED 初始化
  * @retval OLED_Result_t 操作结果
  */
OLED_Result_t OLED_InitSafe(void);

/**
  * @brief  带错误处理的显示字符串
  * @param  x: X 坐标
  * @param  y: Y 坐标
  * @param  str: 字符串
  * @param  size: 字体大小
  * @param  mode: 显示模式
  * @retval OLED_Result_t 操作结果
  */
OLED_Result_t OLED_ShowStringSafe(uint8_t x, uint8_t y, const uint8_t *str,
                                   uint8_t size, uint8_t mode);

/**
  * @brief  带错误处理的刷新显示
  * @retval OLED_Result_t 操作结果
  */
OLED_Result_t OLED_RefreshSafe(void);

/**
  * @brief  检查 OLED 是否正常
  * @retval true=正常, false=异常
  */
bool OLED_IsHealthy(void);

/**
  * @brief  进入降级模式
  * @retval None
  */
void OLED_DegradedMode(void);

/**
  * @brief  设置错误回调
  * @param  callback: 回调函数
  * @retval None
  */
void OLED_SetErrorCallback(DisplayErrorCallback_t callback);

/**
  * @brief  获取当前降级模式
  * @retval DegradeMode_t 当前模式
  */
DegradeMode_t OLED_GetDegradeMode(void);

#ifdef __cplusplus
}
#endif

#endif /* __OLED_WRAPPER_H */
```

- [ ] **Step 2: 烧录验证头文件编译通过**

---

### Task 4.2: 实现 oled_wrapper.c

**Files:**
- Create: `Core/Src/oled_wrapper.c`

- [ ] **Step 1: 编写 oled_wrapper.c 实现**

```c
/**
  ******************************************************************************
  * @file           : oled_wrapper.c
  * @brief          : OLED 封装实现
  ******************************************************************************
  */

#include "oled_wrapper.h"
#include "oled.h"
#include "error_log.h"
#include <stdio.h>
#include <string.h>

/* 外部变量 */
extern UART_HandleTypeDef huart1;
extern I2C_HandleTypeDef hi2c1;

/* 静态变量 */
static OLED_Result_t oled_result;
static DegradeMode_t degrade_mode = DEGRADE_NONE;
static DisplayErrorCallback_t error_callback = NULL;

/* 私有函数声明 */
static void OLED_Wrapper_SendDebug(const char *msg);
static DegradeMode_t OLED_GetDegradeModeFromError(DisplayError_t error);

/* 初始化 */
void OLED_Wrapper_Init(void)
{
    memset(&oled_result, 0, sizeof(OLED_Result_t));
    degrade_mode = DEGRADE_NONE;
    OLED_Wrapper_SendDebug("OLED_Wrapper: Initialized");
}

/* 带错误处理的 OLED 初始化 */
OLED_Result_t OLED_InitSafe(void)
{
    for (int retry = 0; retry < OLED_MAX_RETRIES; retry++) {
        /* 尝试初始化 */
        OLED_Init();
        
        /* 验证 I2C 通信 */
        uint8_t test_data = 0;
        oled_result.i2c_status = HAL_I2C_Master_Transmit(&hi2c1, 0x78, &test_data, 1, OLED_I2C_TIMEOUT_MS);
        
        if (oled_result.i2c_status == HAL_OK) {
            oled_result.display_ok = true;
            oled_result.error_count = 0;
            degrade_mode = DEGRADE_NONE;
            OLED_Wrapper_SendDebug("OLED_InitSafe: OK");
            return oled_result;
        }
        
        oled_result.error_count++;
        char msg[64];
        snprintf(msg, sizeof(msg), "OLED_InitSafe: Retry %d/%d", retry + 1, OLED_MAX_RETRIES);
        OLED_Wrapper_SendDebug(msg);
        
        HAL_Delay(10);
    }
    
    /* 所有重试失败 */
    oled_result.display_ok = false;
    degrade_mode = DEGRADE_LED_ONLY;
    
    DisplayError_t error = DISP_ERR_I2C_TIMEOUT;
    if (error_callback) {
        error_callback(error);
    }
    
    ErrorLog_Add(ERROR_LOG_OLED_FAIL, ERROR_SEVERITY_ERROR, 0);
    OLED_Wrapper_SendDebug("OLED_InitSafe: FAILED, entering degraded mode");
    
    return oled_result;
}

/* 带错误处理的显示字符串 */
OLED_Result_t OLED_ShowStringSafe(uint8_t x, uint8_t y, const uint8_t *str,
                                   uint8_t size, uint8_t mode)
{
    if (degrade_mode == DEGRADE_SILENT) {
        oled_result.display_ok = false;
        return oled_result;
    }
    
    if (degrade_mode == DEGRADE_LED_ONLY) {
        /* 仅 LED 指示，不操作 OLED */
        oled_result.display_ok = false;
        return oled_result;
    }
    
    /* 正常模式或重试模式 */
    for (int retry = 0; retry < OLED_MAX_RETRIES; retry++) {
        OLED_ShowString(x, y, (uint8_t *)str, size, mode);
        oled_result.display_ok = true;
        return oled_result;
    }
    
    /* 重试失败 */
    oled_result.error_count++;
    if (oled_result.error_count >= OLED_ERROR_THRESHOLD) {
        degrade_mode = DEGRADE_LED_ONLY;
        OLED_Wrapper_SendDebug("OLED: Too many errors, entering LED_ONLY mode");
    }
    
    return oled_result;
}

/* 带错误处理的刷新显示 */
OLED_Result_t OLED_RefreshSafe(void)
{
    if (degrade_mode == DEGRADE_SILENT || degrade_mode == DEGRADE_LED_ONLY) {
        oled_result.display_ok = false;
        return oled_result;
    }
    
    OLED_Refresh();
    oled_result.display_ok = true;
    return oled_result;
}

/* 检查 OLED 是否正常 */
bool OLED_IsHealthy(void)
{
    return oled_result.display_ok && (degrade_mode == DEGRADE_NONE);
}

/* 进入降级模式 */
void OLED_DegradedMode(void)
{
    degrade_mode = DEGRADE_LED_ONLY;
    OLED_Wrapper_SendDebug("OLED: Entering degraded mode (LED only)");
}

/* 设置错误回调 */
void OLED_SetErrorCallback(DisplayErrorCallback_t callback)
{
    error_callback = callback;
}

/* 获取当前降级模式 */
DegradeMode_t OLED_GetDegradeMode(void)
{
    return degrade_mode;
}

/* 调试输出 */
static void OLED_Wrapper_SendDebug(const char *msg)
{
    HAL_UART_Transmit(&huart1, (uint8_t *)msg, strlen(msg), 100);
    HAL_UART_Transmit(&huart1, (uint8_t *)"\r\n", 2, 100);
}
```

- [ ] **Step 2: 烧录测试 OLED 封装功能**

烧录后观察：
1. OLED 显示正常
2. 串口输出 OLED_Wrapper 初始化成功

- [ ] **Step 3: 提交代码**

```bash
git add Core/Inc/oled_wrapper.h Core/Src/oled_wrapper.c
git commit -m "feat: add OLED wrapper with I2C error handling and degraded mode"
```

---

## Task 5: LED 指示增强

**目标:** 增强 LED 状态指示，支持多种闪烁模式

**依赖:** 无

**测试验证:** LED 按不同模式闪烁

---

### Task 5.1: 创建 led指示.h 和 led指示.c

**Files:**
- Create: `Core/Inc/led指示.h`
- Create: `Core/Src/led指示.c`

- [ ] **Step 1: 编写 led指示.h**

```c
/**
  ******************************************************************************
  * @file           : led指示.h
  * @brief          : LED 指示增强接口
  ******************************************************************************
  */

#ifndef __LED指示_H
#define __LED指示_H

#ifdef __cplusplus
extern "C" {
#endif

#include "stm32f4xx_hal.h"
#include <stdint.h>
#include <stdbool.h>

/* LED 状态定义 */
typedef enum {
    LED_STATE_OFF = 0,              // 熄灭
    LED_STATE_ON,                   // 常亮
    LED_STATE_BLINK_SLOW,           // 慢闪 (500ms)
    LED_STATE_BLINK_FAST,           // 快闪 (100ms)
    LED_STATE_BLINK_ERROR,          // 错误闪烁 (3次快闪后暂停)
    LED_STATE_BLINK_PROGRESS        // 进度闪烁
} LED_State_t;

/* LED 配置 */
#define LED_PORT                GPIOB
#define LED_PIN                 GPIO_PIN_2
#define LED_BLINK_SLOW_MS       500
#define LED_BLINK_FAST_MS       100
#define LED_ERROR_PAUSE_MS      1000

/* 函数声明 */

/**
  * @brief  初始化 LED 指示
  * @retval None
  */
void LED_Indicator_Init(void);

/**
  * @brief  设置 LED 状态
  * @param  state: LED 状态
  * @retval None
  */
void LED_SetState(LED_State_t state);

/**
  * @brief  LED 任务 (在主循环中调用)
  * @retval None
  */
void LED_Task(void);

/**
  * @brief  LED 错误指示
  * @param  error_code: 错误代码 (闪烁次数)
  * @retval None
  */
void LED_ShowError(uint8_t error_code);

/**
  * @brief  获取当前 LED 状态
  * @retval LED_State_t 当前状态
  */
LED_State_t LED_GetState(void);

#ifdef __cplusplus
}
#endif

#endif /* __LED指示_H */
```

- [ ] **Step 2: 编写 led指示.c**

```c
/**
  ******************************************************************************
  * @file           : led指示.c
  * @brief          : LED 指示增强实现
  ******************************************************************************
  */

#include "led指示.h"

/* 静态变量 */
static LED_State_t current_state = LED_STATE_OFF;
static uint32_t last_toggle_tick = 0;
static uint8_t error_blink_count = 0;
static uint8_t error_blink_target = 0;

/* 初始化 */
void LED_Indicator_Init(void)
{
    current_state = LED_STATE_OFF;
    last_toggle_tick = HAL_GetTick();
    error_blink_count = 0;
    
    /* 确保 LED 熄灭 (高电平熄灭) */
    HAL_GPIO_WritePin(LED_PORT, LED_PIN, GPIO_PIN_SET);
}

/* 设置 LED 状态 */
void LED_SetState(LED_State_t state)
{
    current_state = state;
    last_toggle_tick = HAL_GetTick();
    error_blink_count = 0;
    
    switch (state) {
        case LED_STATE_OFF:
            HAL_GPIO_WritePin(LED_PORT, LED_PIN, GPIO_PIN_SET);
            break;
        case LED_STATE_ON:
            HAL_GPIO_WritePin(LED_PORT, LED_PIN, GPIO_PIN_RESET);
            break;
        default:
            break;
    }
}

/* LED 任务 */
void LED_Task(void)
{
    uint32_t now = HAL_GetTick();
    
    switch (current_state) {
        case LED_STATE_OFF:
            HAL_GPIO_WritePin(LED_PORT, LED_PIN, GPIO_PIN_SET);
            break;
            
        case LED_STATE_ON:
            HAL_GPIO_WritePin(LED_PORT, LED_PIN, GPIO_PIN_RESET);
            break;
            
        case LED_STATE_BLINK_SLOW:
            if ((now - last_toggle_tick) >= LED_BLINK_SLOW_MS) {
                HAL_GPIO_TogglePin(LED_PORT, LED_PIN);
                last_toggle_tick = now;
            }
            break;
            
        case LED_STATE_BLINK_FAST:
            if ((now - last_toggle_tick) >= LED_BLINK_FAST_MS) {
                HAL_GPIO_TogglePin(LED_PORT, LED_PIN);
                last_toggle_tick = now;
            }
            break;
            
        case LED_STATE_BLINK_ERROR:
            if (error_blink_target == 0) {
                /* 未启动错误闪烁 */
                break;
            }
            
            if (error_blink_count < error_blink_target * 2) {
                /* 闪烁阶段 */
                if ((now - last_toggle_tick) >= LED_BLINK_FAST_MS) {
                    HAL_GPIO_TogglePin(LED_PORT, LED_PIN);
                    last_toggle_tick = now;
                    error_blink_count++;
                }
            } else {
                /* 暂停阶段 */
                if ((now - last_toggle_tick) >= LED_ERROR_PAUSE_MS) {
                    error_blink_count = 0;
                    last_toggle_tick = now;
                }
            }
            break;
            
        case LED_STATE_BLINK_PROGRESS:
            /* 进度闪烁，由外部控制 */
            if ((now - last_toggle_tick) >= LED_BLINK_FAST_MS) {
                HAL_GPIO_TogglePin(LED_PORT, LED_PIN);
                last_toggle_tick = now;
            }
            break;
    }
}

/* LED 错误指示 */
void LED_ShowError(uint8_t error_code)
{
    error_blink_target = error_code;
    error_blink_count = 0;
    current_state = LED_STATE_BLINK_ERROR;
    last_toggle_tick = HAL_GetTick();
}

/* 获取当前 LED 状态 */
LED_State_t LED_GetState(void)
{
    return current_state;
}
```

- [ ] **Step 3: 烧录测试 LED 指示功能**

烧录后观察 LED 按不同模式闪烁

- [ ] **Step 4: 提交代码**

```bash
git add Core/Inc/led指示.h Core/Src/led指示.c
git commit -m "feat: add LED indicator with multiple blink modes"
```

---

## Task 6: 集成到现有模块

**目标:** 将新模块集成到 bootloader 和 main.c

**依赖:** Task 1-5

**测试验证:** 完整 OTA 流程测试，掉电恢复测试

---

### Task 6.1: 修改 bootloader.h

**Files:**
- Modify: `Core/Inc/bootloader.h`

- [ ] **Step 1: 在 bootloader.h 中添加新定义**

在文件末尾 `#endif` 之前添加：

```c
/* 新增：错误恢复策略 */
typedef enum {
    RECOVERY_NONE = 0,
    RECOVERY_RETRY,
    RECOVERY_SKIP,
    RECOVERY_ROLLBACK,
    RECOVERY_MANUAL
} RecoveryAction_t;

/* 新增：OTA 控制块 (扩展) */
typedef struct {
    uint32_t state;                 // OTA_State_t
    uint32_t received_size;
    uint32_t total_size;
    uint32_t crc32;
    uint32_t partition_addr;
    uint32_t error_count;
    uint32_t last_error_tick;
    uint8_t retry_count;
    uint8_t max_retries;
} OTA_ControlBlock_t;

/* 新增：掉电保护地址 */
#define OTA_STATE_ADDR              0x0800BFD4U
#define OTA_RECEIVED_SIZE_ADDR      0x0800BFCFU

/* 新增：恢复策略函数 */
RecoveryAction_t Bootloader_GetRecoveryAction(BootError_t error, uint8_t retry_count);
bool Bootloader_ExecuteRecovery(RecoveryAction_t action);
```

- [ ] **Step 2: 提交代码**

```bash
git add Core/Inc/bootloader.h
git commit -m "feat: add recovery strategy definitions to bootloader"
```

---

### Task 6.2: 修改 bootloader.c

**Files:**
- Modify: `Core/Src/bootloader.c`

- [ ] **Step 1: 在文件顶部添加新头文件包含**

```c
#include "flash_driver.h"
#include "error_log.h"
#include "protocol.h"
#include "oled_wrapper.h"
#include "led指示.h"
```

- [ ] **Step 2: 修改 Bootloader_Init 函数**

在函数末尾添加：

```c
    /* 初始化新模块 */
    FlashDriver_Init();
    ErrorLog_Init();
    Protocol_Init();
    OLED_Wrapper_Init();
    LED_Indicator_Init();
    
    /* 检查掉电恢复 */
    if (Bootloader_IsPowerLossRecovery()) {
        Bootloader_SendResponse("Power loss detected, recovering...");
        Bootloader_HandlePowerLoss();
    }
```

- [ ] **Step 3: 添加掉电恢复函数**

在文件末尾添加：

```c
/* 检查是否需要掉电恢复 */
bool Bootloader_IsPowerLossRecovery(void)
{
    uint32_t ota_state = *(volatile uint32_t *)OTA_STATE_ADDR;
    return (ota_state == 0x00000002);  // OTA_STATE_WRITING
}

/* 处理掉电恢复 */
void Bootloader_HandlePowerLoss(void)
{
    /* 清除 OTA 状态 */
    uint32_t active = *(volatile uint32_t *)APP_ACTIVE_ADDR;
    uint32_t crc = *(volatile uint32_t *)APP_CRC_ADDR;
    uint32_t size = *(volatile uint32_t *)APP_SIZE_ADDR;
    
    __disable_irq();
    HAL_FLASH_Unlock();
    
    FLASH_EraseInitTypeDef erase_init;
    uint32_t sector_error = 0;
    erase_init.TypeErase = FLASH_TYPEERASE_SECTORS;
    erase_init.Sector = FLASH_SECTOR_2;
    erase_init.NbSectors = 1;
    erase_init.VoltageRange = FLASH_VOLTAGE_RANGE_3;
    HAL_FLASHEx_Erase(&erase_init, &sector_error);
    
    /* 重写控制数据 (不清除 OTA 状态，让应用层决定) */
    if (*(volatile uint32_t *)BOOT_CONTROL_MAGIC_ADDR == BOOT_CONTROL_MAGIC) {
        HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD, BOOT_CONTROL_MAGIC_ADDR, BOOT_CONTROL_MAGIC);
    }
    HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD, APP_ACTIVE_ADDR, active);
    HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD, APP_CRC_ADDR, crc);
    HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD, APP_SIZE_ADDR, size);
    
    HAL_FLASH_Lock();
    __enable_irq();
    
    Bootloader_SendResponse("Power loss recovery done");
}

/* 获取恢复策略 */
RecoveryAction_t Bootloader_GetRecoveryAction(BootError_t error, uint8_t retry_count)
{
    if (retry_count < 3) {
        return RECOVERY_RETRY;
    }
    
    switch (error) {
        case BOOT_ERR_FLASH_ERASE:
        case BOOT_ERR_FLASH_WRITE:
        case BOOT_ERR_CRC_MISMATCH:
            return RECOVERY_ROLLBACK;
        case BOOT_ERR_UART_TIMEOUT:
            return RECOVERY_MANUAL;
        case BOOT_ERR_I2C_TIMEOUT:
        case BOOT_ERR_OLED_FAIL:
            return RECOVERY_NONE;  // 显示错误可忽略
        default:
            return RECOVERY_MANUAL;
    }
}

/* 执行恢复 */
bool Bootloader_ExecuteRecovery(RecoveryAction_t action)
{
    switch (action) {
        case RECOVERY_RETRY:
            return true;  // 由调用者重试
        case RECOVERY_ROLLBACK:
            Bootloader_SetRollbackFlag();
            return true;
        case RECOVERY_MANUAL:
            Bootloader_SendResponse("Manual intervention required");
            return false;
        default:
            return true;
    }
}
```

- [ ] **Step 4: 提交代码**

```bash
git add Core/Src/bootloader.c
git commit -m "feat: integrate error handling modules into bootloader"
```

---

### Task 6.3: 修改 main.c

**Files:**
- Modify: `Core/Src/main.c`

- [ ] **Step 1: 在文件顶部添加新头文件包含**

```c
#include "flash_driver.h"
#include "error_log.h"
#include "protocol.h"
#include "oled_wrapper.h"
#include "led指示.h"
```

- [ ] **Step 2: 在 main 函数中添加新模块初始化**

在 `MX_I2C1_Init()` 之后添加：

```c
  /* 初始化新模块 */
  FlashDriver_Init();
  ErrorLog_Init();
  Protocol_Init();
  LED_Indicator_Init();
```

- [ ] **Step 3: 提交代码**

```bash
git add Core/Src/main.c
git commit -m "feat: initialize error handling modules in main"
```

---

## Task 7: 集成测试

**目标:** 验证完整 OTA 流程和错误恢复

**依赖:** Task 1-6

**测试验证:** 完整功能测试

---

### Task 7.1: 完整 OTA 流程测试

- [ ] **Step 1: 烧录最新代码**

编译并烧录到开发板

- [ ] **Step 2: 测试正常 OTA 流程**

通过串口发送以下命令：
```
ota_enter
reset
ota_start 1024
ota_data <hex数据>
ota_end <crc32>
```

预期结果：
- OLED 显示进度
- 串口输出 ACK
- 最后跳转到新固件

- [ ] **Step 3: 测试掉电恢复**

1. 开始 OTA 更新
2. 在写入过程中断电
3. 重新上电
4. 观察是否自动恢复到 bootloader 模式

- [ ] **Step 4: 测试通信错误恢复**

1. 发送错误的 CRC 数据
2. 观察是否自动重试或回滚

- [ ] **Step 5: 测试 OLED 降级**

1. 断开 OLED I2C 连接
2. 观察是否降级到 LED 指示

- [ ] **Step 6: 提交测试报告**

```bash
git add docs/test-report-2026-07-24.md
git commit -m "docs: add integration test report"
```

---

## 实施顺序总结

```
Task 1: Flash 驱动封装 (硬件层)        ← 第 1 天测试
Task 2: 错误日志系统 (日志层)          ← 第 1 天测试
Task 3: 通信协议增强 (协议层)          ← 第 2 天测试
Task 4: OLED 封装 (显示层)             ← 第 2 天测试
Task 5: LED 指示增强                   ← 第 2 天测试
Task 6: 集成到现有模块                 ← 第 3 天测试
Task 7: 集成测试                       ← 第 3 天完成
```

**预估总工时:** 12-18 小时 (3 天)
