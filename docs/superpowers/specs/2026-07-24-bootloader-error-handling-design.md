# Bootloader 分层错误处理系统设计

**日期**: 2026-07-24  
**版本**: 1.0  
**状态**: 待审批

---

## 1. 背景与目标

### 1.1 当前问题

STM32F407VETx Bootloader 项目存在以下错误处理缺陷：

1. **Flash 磨损风险**: `Bootloader_LogError()` 每次错误都写 Flash，频繁出错会磨损 Flash
2. **掉电无保护**: OTA 写入过程中断电，固件处于不一致状态，无法恢复
3. **通信无重试**: UART 传输失败直接报错，没有重传机制
4. **外设错误静默**: OLED/I2C 操作没有错误检查，故障时无法恢复

### 1.2 设计目标

- 实现分层错误处理架构，职责清晰
- 增加掉电保护机制，OTA 过程可恢复
- 增加通信重试机制，提高传输可靠性
- OLED 操作增加错误处理，支持降级显示
- 优化错误日志系统，减少 Flash 磨损

---

## 2. 架构设计

### 2.1 分层架构

```
┌─────────────────────────────────────────────────────────┐
│                    应用层 (Application)                  │
│  OTA 状态机增强、掉电保护、错误恢复策略                      │
├─────────────────────────────────────────────────────────┤
│                    协议层 (Protocol)                      │
│  UART 通信协议、ACK/NACK 握手、重传机制、超时控制            │
├─────────────────────────────────────────────────────────┤
│                    硬件层 (Hardware)                      │
│  Flash/I2C/UART 驱动封装、自动重试、错误检测               │
├─────────────────────────────────────────────────────────┤
│                    显示层 (Display)                       │
│  OLED 封装、I2C 错误处理、降级显示、LED 指示增强            │
├─────────────────────────────────────────────────────────┤
│                    日志层 (Logging)                       │
│  RAM 缓冲、批量刷写、FATAL 立即写入、错误统计               │
└─────────────────────────────────────────────────────────┘
```

### 2.2 模块职责

| 模块 | 职责 | 依赖 |
|------|------|------|
| 硬件层 | 封装底层驱动，提供重试和错误检测 | HAL 库 |
| 协议层 | UART 通信协议，数据校验和重传 | 硬件层 |
| 应用层 | OTA 流程控制，状态管理，恢复策略 | 协议层、硬件层、日志层 |
| 显示层 | OLED 显示封装，降级处理 | 硬件层 |
| 日志层 | 错误记录，批量写入 | 硬件层 |

---

## 3. 硬件层设计

### 3.1 错误等级定义

```c
typedef enum {
    ERR_LVL_WARN = 0,    // 警告，可以继续
    ERR_LVL_ERROR = 1,   // 错误，需要重试
    ERR_LVL_FATAL = 2    // 致命错误，必须停止
} ErrorLevel_t;
```

### 3.2 Flash 操作封装

```c
typedef struct {
    HAL_StatusTypeDef status;   // HAL 返回值
    uint32_t retries;           // 重试次数
    ErrorLevel_t level;         // 错误等级
    const char *msg;            // 错误描述
} FlashResult_t;

FlashResult_t Flash_WriteWord(uint32_t addr, uint32_t data, uint8_t max_retries);
FlashResult_t Flash_EraseSector(uint32_t sector, uint8_t max_retries);
```

**重试策略**:
- 默认重试 3 次，每次间隔 10ms
- 第 1 次失败：WARN，继续重试
- 第 2 次失败：ERROR，记录日志
- 第 3 次失败：FATAL，停止操作

### 3.3 I2C 通信封装

```c
typedef struct {
    HAL_StatusTypeDef status;
    uint8_t retries;
    ErrorLevel_t level;
} I2CResult_t;

I2CResult_t I2C_SendData(uint8_t dev_addr, uint8_t *data, uint16_t len, uint8_t max_retries);
```

### 3.4 UART 通信封装

```c
typedef struct {
    HAL_StatusTypeDef status;
    uint32_t bytes_sent;
    ErrorLevel_t level;
} UARTResult_t;

UARTResult_t UART_SendMessage(UART_HandleTypeDef *huart, const char *msg, uint32_t timeout_ms);
```

---

## 4. 协议层设计

### 4.1 通信协议增强

```c
typedef enum {
    CMD_OTA_ENTER = 0,
    CMD_OTA_START,
    CMD_OTA_DATA,
    CMD_OTA_END,
    CMD_OTA_STATUS,
    CMD_RESET,
    CMD_VERSION,
    CMD_ACK,        // 确认
    CMD_NACK,       // 否认，请求重传
    CMD_PING        // 心跳检测
} ProtocolCmd_t;

typedef struct {
    uint8_t seq;            // 序列号 (0-255 循环)
    uint8_t cmd;            // 命令类型
    uint16_t len;           // 数据长度
    uint32_t crc32;         // 包头+CRC
    uint8_t data[];         // 数据 payload
} ProtocolPacket_t;
```

### 4.2 重传机制

```c
typedef struct {
    uint8_t expected_seq;       // 期望的序列号
    uint32_t received_size;     // 已接收大小
    uint32_t total_size;        // 总大小
    uint8_t retry_count;        // 当前重试次数
    uint8_t max_retries;        // 最大重试次数 (默认 5)
    uint32_t timeout_ms;        // 单次超时 (默认 1000ms)
} OTA_TransferState_t;

bool OTA_ProcessDataWithRetry(const uint8_t *data, uint32_t len, uint8_t seq);
```

### 4.3 超时控制

```c
#define OTA_TIMEOUT_MS          30000   // 30秒无数据超时
#define OTA_ACK_TIMEOUT_MS      1000    // ACK 等待超时
#define OTA_RETRY_INTERVAL_MS   100     // 重试间隔

bool OTA_CheckTimeout(void);
```

---

## 5. 应用层设计

### 5.1 OTA 状态机

```c
typedef enum {
    OTA_STATE_IDLE = 0,
    OTA_STATE_ERASING,
    OTA_STATE_WRITING,
    OTA_STATE_VERIFYING,
    OTA_STATE_COMPLETE,
    OTA_STATE_ERROR,
    OTA_STATE_ROLLBACK
} OTA_State_t;

typedef struct {
    OTA_State_t state;
    OTA_State_t prev_state;
    uint32_t received_size;
    uint32_t total_size;
    uint32_t crc32;
    uint32_t partition_addr;
    uint32_t error_count;
    uint32_t last_error_tick;
    uint8_t retry_count;
    uint8_t max_retries;
} OTA_ControlBlock_t;
```

### 5.2 掉电保护机制

```c
#define OTA_STATE_ADDR          0x0800BFD4U
#define OTA_RECEIVED_SIZE_ADDR  0x0800BFCFU

void OTA_BeginWrite(void);
void OTA_EndWrite(void);
bool OTA_IsInProgress(void);
```

**掉电恢复流程**:
1. 检查 `OTA_STATE` 是否为 `OTA_STATE_WRITING`
2. 如果是，说明上次写入中断
3. 比较 `OTA_RECEIVED_SIZE` 与实际 Flash 数据
4. 如果不一致，说明掉电了
5. 选择：重新擦除并重试 OR 回滚到上一个固件

### 5.3 错误恢复策略

```c
typedef enum {
    RECOVERY_NONE = 0,
    RECOVERY_RETRY,
    RECOVERY_SKIP,
    RECOVERY_ROLLBACK,
    RECOVERY_MANUAL
} RecoveryAction_t;

RecoveryAction_t OTA_GetRecoveryAction(BootError_t error, uint8_t retry_count);
bool OTA_ExecuteRecovery(RecoveryAction_t action);
```

**恢复策略表**:

| 错误类型 | 重试次数 < 3 | 重试次数 >= 3 |
|---------|-------------|--------------|
| Flash 写入失败 | 重试 | 回滚 |
| Flash 擦除失败 | 重试 | 回滚 |
| CRC 校验失败 | 跳过（重传） | 回滚 |
| UART 超时 | 重试 | 人工干预 |
| OLED 显示错误 | 忽略 | 忽略 |
| 掉电恢复 | 重试 | 回滚 |

---

## 6. 显示层设计

### 6.1 OLED 封装

```c
typedef struct {
    HAL_StatusTypeDef i2c_status;
    bool display_ok;
    uint8_t error_count;
} OLED_Result_t;

OLED_Result_t OLED_InitSafe(void);
OLED_Result_t OLED_ShowStringSafe(uint8_t x, uint8_t y, const uint8_t *str, 
                                   uint8_t size, uint8_t mode);
OLED_Result_t OLED_RefreshSafe(void);
bool OLED_IsHealthy(void);
void OLED_DegradedMode(void);
```

### 6.2 显示错误处理

```c
typedef enum {
    DISP_ERR_NONE = 0,
    DISP_ERR_I2C_TIMEOUT,
    DISP_ERR_I2C_NACK,
    DISP_ERR_BUFFER_FULL,
    DISP_ERR_INVALID_PARAM
} DisplayError_t;

typedef void (*DisplayErrorCallback_t)(DisplayError_t error);

void OLED_SetErrorCallback(DisplayErrorCallback_t callback);
void OLED_DefaultErrorHandler(DisplayError_t error);
```

### 6.3 降级显示策略

```c
typedef enum {
    DEGRADE_NONE = 0,
    DEGRADE_RETRY,
    DEGRADE_LED_ONLY,
    DEGRADE_SILENT
} DegradeMode_t;

DegradeMode_t OLED_GetDegradeMode(DisplayError_t error);
void OLED_ApplyDegradeMode(DegradeMode_t mode);
```

**降级策略**:

| 错误类型 | 处理方式 |
|---------|---------|
| I2C 超时 | 重试 3 次，失败则 LED 闪烁 |
| I2C NACK | 重试 2 次，失败则静默模式 |
| 连续 5 次错误 | 进入静默模式，仅 LED 指示 |

### 6.4 LED 指示增强

```c
typedef enum {
    LED_STATE_OFF = 0,
    LED_STATE_ON,
    LED_STATE_BLINK_SLOW,
    LED_STATE_BLINK_FAST,
    LED_STATE_BLINK_ERROR,
    LED_STATE_BLINK_PROGRESS
} LED_State_t;

void LED_SetState(LED_State_t state);
void LED_Task(void);
void LED_ShowError(uint8_t error_code);
```

---

## 7. 日志层设计

### 7.1 RAM 缓冲 + 批量写入

```c
#define ERROR_LOG_RAM_SIZE      16
#define ERROR_LOG_FLASH_SIZE    8
#define ERROR_LOG_FLUSH_THRESHOLD 5

typedef struct {
    uint32_t error_code;
    uint32_t timestamp;
    uint32_t state;
    uint8_t severity;
    uint8_t retry_count;
    uint16_t line;
} ErrorLogEntry_t;

typedef struct {
    ErrorLogEntry_t ram_buffer[ERROR_LOG_RAM_SIZE];
    uint8_t ram_count;
    uint8_t flash_count;
    uint32_t total_errors;
    bool needs_flush;
} ErrorLogManager_t;
```

### 7.2 日志操作函数

```c
void ErrorLog_Init(void);
void ErrorLog_Add(uint32_t error_code, uint8_t severity, uint16_t line);
void ErrorLog_Flush(void);
uint32_t ErrorLog_Read(ErrorLogEntry_t *entries, uint32_t max_count);
void ErrorLog_Clear(void);
void ErrorLog_GetStats(uint32_t *total, uint32_t *fatal_count);
```

### 7.3 刷写策略

**刷写时机**:
1. RAM 缓冲满（>= `ERROR_LOG_RAM_SIZE`）
2. 累积 >= `ERROR_LOG_FLUSH_THRESHOLD` 条
3. 发生 FATAL 错误时立即刷写
4. 系统空闲时定期刷写（每 30 秒）

---

## 8. 文件结构

### 8.1 新增文件

| 文件路径 | 说明 |
|---------|------|
| `Core/Inc/error_log.h` | 错误日志系统接口 |
| `Core/Src/error_log.c` | 错误日志实现 |
| `Core/Inc/protocol.h` | 通信协议接口 |
| `Core/Src/protocol.c` | 通信协议实现 |
| `Core/Inc/flash_driver.h` | Flash 操作封装 |
| `Core/Src/flash_driver.c` | Flash 重试逻辑 |

### 8.2 修改文件

| 文件路径 | 改动内容 |
|---------|---------|
| `Core/Inc/bootloader.h` | 新增错误等级、OTA 状态、恢复策略定义 |
| `Core/Src/bootloader.c` | 重构错误处理、增加重试逻辑、掉电保护 |
| `Core/Inc/display_ota.h` | 新增显示错误类型、降级模式定义 |
| `Core/Src/display_ota.c` | OLED 封装、I2C 错误处理、降级显示 |
| `Core/Src/main.c` | 集成新模块、启动时恢复状态 |

---

## 9. 测试计划

### 9.1 单元测试

- Flash 重试逻辑测试
- 协议 ACK/NACK 握手测试
- 错误日志 RAM 缓冲和刷写测试
- 状态机转换测试

### 9.2 集成测试

- OTA 完整流程测试（正常、掉电、通信错误）
- OLED 降级显示测试
- 错误恢复策略测试

### 9.3 压力测试

- 频繁掉电恢复测试
- 长时间 OTA 传输稳定性测试
- Flash 磨损验证

---

## 10. 预估工作量

| 模块 | 预估时间 |
|------|---------|
| 硬件层封装 | 2-3 小时 |
| 协议层增强 | 3-4 小时 |
| 应用层状态机 | 2-3 小时 |
| 显示层封装 | 1-2 小时 |
| 日志系统 | 1-2 小时 |
| 集成测试 | 3-4 小时 |
| **总计** | **12-18 小时** |

---

## 11. 风险与缓解

| 风险 | 影响 | 缓解措施 |
|------|------|---------|
| 状态机复杂度增加 | 可能引入新 bug | 充分测试每个状态转换 |
| Flash 频繁擦写 | 缩短寿命 | RAM 缓冲 + 批量写入 |
| 协议变更 | 上位机兼容性问题 | 保持向后兼容，增加版本协商 |
| 内存占用增加 | 可能超出 RAM 限制 | 精心设计数据结构，使用静态分配 |

---

## 附录 A: 错误代码定义

```c
typedef enum {
    BOOT_ERR_NONE = 0,
    BOOT_ERR_FLASH_ERASE,
    BOOT_ERR_FLASH_WRITE,
    BOOT_ERR_CRC_MISMATCH,
    BOOT_ERR_INVALID_APP,
    BOOT_ERR_UART_TIMEOUT,
    BOOT_ERR_UART_OVERFLOW,
    BOOT_ERR_WATCHDOG_RESET,
    BOOT_ERR_POWER_LOW,
    BOOT_ERR_ROLLBACK_FAILED,
    BOOT_ERR_I2C_TIMEOUT,       // 新增
    BOOT_ERR_I2C_NACK,          // 新增
    BOOT_ERR_OLED_FAIL,         // 新增
    BOOT_ERR_PROTOCOL_NACK,     // 新增
    BOOT_ERR_PROTOCOL_CRC,      // 新增
    BOOT_ERR_UNKNOWN
} BootError_t;
```

## 附录 B: 参考资料

1. STM32F407 Reference Manual (RM0090)
2. STM32F4 Flash Programming Manual
3. 现有代码：`bootloader.c`, `display_ota.c`, `main.c`
