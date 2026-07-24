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
#define ERROR_LOG_FLASH_SIZE        4       // Flash 保留条目数 (entries 0-3, 避免与控制数据地址重叠)
#define ERROR_LOG_FLUSH_THRESHOLD   5       // 触发刷写的阈值

/* 错误日志管理器 */
typedef struct {
    ErrorLogEntry_t ram_buffer[ERROR_LOG_RAM_SIZE];
    uint8_t ram_count;
    uint8_t flash_count;
    uint32_t fatal_count;
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
