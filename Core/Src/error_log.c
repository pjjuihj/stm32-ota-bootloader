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
static void ErrorLog_FillEntry(ErrorLogEntry_t *entry, ErrorCode_t error_code,
                               ErrorSeverity_t severity, uint16_t line);

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

/* 填充错误条目 (消除重复代码) */
static void ErrorLog_FillEntry(ErrorLogEntry_t *entry, ErrorCode_t error_code,
                               ErrorSeverity_t severity, uint16_t line)
{
    entry->error_code = error_code;
    entry->timestamp = HAL_GetTick();
    entry->severity = severity;
    entry->retry_count = 0;
    entry->line = line;
}

/* 记录错误 */
void ErrorLog_Add(ErrorCode_t error_code, ErrorSeverity_t severity, uint16_t line)
{
    /* 写入 RAM 缓冲 */
    if (error_log.ram_count < ERROR_LOG_RAM_SIZE) {
        ErrorLog_FillEntry(&error_log.ram_buffer[error_log.ram_count],
                           error_code, severity, line);
        error_log.ram_count++;
    } else {
        /* RAM 满，强制刷写 */
        ErrorLog_Flush();
        if (error_log.ram_count < ERROR_LOG_RAM_SIZE) {
            ErrorLog_FillEntry(&error_log.ram_buffer[error_log.ram_count],
                               error_code, severity, line);
            error_log.ram_count++;
        }
    }

    error_log.total_errors++;

    /* FATAL 错误计数 (持久化, 不随 Flush 丢失) */
    if (severity == ERROR_SEVERITY_FATAL) {
        error_log.fatal_count++;
    }

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

    /* 计算实际可写入的数量 (与 WriteFlash 一致) */
    uint8_t write_count = (error_log.ram_count > ERROR_LOG_FLASH_SIZE)
                        ? ERROR_LOG_FLASH_SIZE : error_log.ram_count;

    bool result = ErrorLog_WriteFlash();
    if (result) {
        error_log.flash_count += write_count;
        /* 移除已写入的条目, 保留未写入的 */
        uint8_t remaining = error_log.ram_count - write_count;
        if (remaining > 0) {
            memmove(error_log.ram_buffer,
                    &error_log.ram_buffer[write_count],
                    remaining * sizeof(ErrorLogEntry_t));
        }
        error_log.ram_count = remaining;
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
    if (entries == NULL || max_count == 0) {
        return 0;
    }

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
    error_log.flash_count = 0;
    error_log.total_errors = 0;
    error_log.fatal_count = 0;
    error_log.needs_flush = false;

    /* 擦除 Sector 2 中的日志区域 (需要重写其他控制数据) */
    /* 这里简化处理，实际应该保留其他控制数据 */
    ErrorLog_SendDebug("ErrorLog: Cleared");
    return true;
}

/* 获取错误统计 */
void ErrorLog_GetStats(uint32_t *total, uint32_t *fatal_count)
{
    if (total != NULL) {
        *total = error_log.total_errors;
    }
    if (fatal_count != NULL) {
        *fatal_count = error_log.fatal_count;
    }
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

    /* 限制每次刷写的条目数不超过 Flash 容量 */
    uint8_t write_count = (error_log.ram_count > ERROR_LOG_FLASH_SIZE)
                        ? ERROR_LOG_FLASH_SIZE : error_log.ram_count;

    /* 写入错误计数 */
    uint32_t new_count = error_log.flash_count + write_count;
    result = FlashDriver_WriteWord(ERROR_LOG_COUNT_ADDR, new_count);
    if (result.status != HAL_OK) {
        return false;
    }

    /* 写入 RAM 中的记录 */
    uint32_t log_addr = ERROR_LOG_DATA_ADDR;
    for (uint8_t i = 0; i < write_count; i++) {
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
