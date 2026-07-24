/**
  ******************************************************************************
  * @file           : bootloader.h
  * @brief          : Bootloader 接口定义
  ******************************************************************************
  * @attention
  *
  * UART Bootloader，用于接收固件并写入 Flash
  * 支持完整备份和回滚功能
  *
  ******************************************************************************
  */

#ifndef __BOOTLOADER_H
#define __BOOTLOADER_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include "stm32f4xx_hal.h"
#include "error_log.h"
#include <stdint.h>
#include <stdbool.h>

/* Exported types ------------------------------------------------------------*/

/**
  * @brief Bootloader 状态
  */
typedef enum {
    BOOT_STATE_IDLE = 0,         /* 空闲，等待命令 */
    BOOT_STATE_RECEIVING,        /* 接收固件中 */
    BOOT_STATE_VERIFYING,        /* 校验固件 */
    BOOT_STATE_JUMPING,          /* 跳转到 App */
    BOOT_STATE_ROLLBACK,         /* 回滚中 */
    BOOT_STATE_ERROR             /* 错误状态 */
} BootState_t;

/**
  * @brief 错误代码
  */
typedef enum {
    BOOT_ERR_NONE = 0,           /* 无错误 */
    BOOT_ERR_FLASH_ERASE,        /* Flash 擦除失败 */
    BOOT_ERR_FLASH_WRITE,        /* Flash 写入失败 */
    BOOT_ERR_CRC_MISMATCH,       /* CRC 校验失败 */
    BOOT_ERR_INVALID_APP,        /* App 无效 */
    BOOT_ERR_UART_TIMEOUT,       /* UART 超时 */
    BOOT_ERR_UART_OVERFLOW,      /* UART 缓冲区溢出 */
    BOOT_ERR_WATCHDOG_RESET,     /* 看门狗复位 */
    BOOT_ERR_POWER_LOW,          /* 电压过低 */
    BOOT_ERR_ROLLBACK_FAILED,    /* 回滚失败 */
    BOOT_ERR_I2C_TIMEOUT,        /* I2C 超时 */
    BOOT_ERR_OLED_FAIL,          /* OLED 显示失败 */
    BOOT_ERR_UNKNOWN             /* 未知错误 */
} BootError_t;

/* ErrorLogEntry_t 定义在 error_log.h 中，避免重复定义 */

/**
  * @brief Bootloader 配置
  */
typedef struct {
    BootState_t state;           /* 当前状态 */
    uint32_t app_size;           /* 固件大小 */
    uint32_t received_size;      /* 已接收大小 */
    uint32_t app_crc;            /* 固件 CRC32 */
    uint32_t calculated_crc;     /* 计算的 CRC32 */
    uint8_t retry_count;         /* 重试次数 */
    uint8_t max_retries;         /* 最大重试次数 */
    uint32_t error_count;        /* 错误计数 */
    uint32_t uart_error_count;   /* UART 错误计数 */
    BootError_t last_error;      /* 最后错误 */
} BootConfig_t;

/**
  * @brief Bootloader 版本信息
  */
typedef struct {
    uint8_t major;               /* 主版本号 */
    uint8_t minor;               /* 次版本号 */
    uint8_t patch;               /* 补丁号 */
    uint8_t reserved;            /* 保留 */
    uint32_t build_date;         /* 构建日期 */
    uint32_t crc;                /* 版本信息 CRC */
} BootVersion_t;

/* Exported constants --------------------------------------------------------*/

/*
 * STM32F407VETx A/B 分区布局 (512KB Flash)
 *
 * ┌──────────────────────────────────────┐
 * │ Bootloader (32KB)   │ Sector 0-1     │ 0x08000000
 * ├──────────────────────┼────────────────┤
 * │ Partition A (224KB)  │ Sector 2-5     │ 0x08008000
 * ├──────────────────────┼────────────────┤
 * │ Partition B (256KB)  │ Sector 6-7     │ 0x08040000
 * └──────────────────────┴────────────────┘
 */

/* Flash 地址定义 */
#define BOOTLOADER_START_ADDR   0x08000000U     /* Bootloader 起始地址 */
#define BOOTLOADER_SIZE         0x8000U         /* Bootloader 大小 (32KB) */

/* A/B 分区地址 - 注意：Sector 2 是控制数据区，不能用作分区！ */
#define PARTITION_A_ADDR        0x0800C000U     /* 分区 A 起始地址 (Sector 3) */
#define PARTITION_A_SIZE        0x34000U        /* 分区 A 大小 (208KB, Sector 3-5) */
#define PARTITION_B_ADDR        0x08040000U     /* 分区 B 起始地址 (Sector 6) */
#define PARTITION_B_SIZE        0x40000U        /* 分区 B 大小 (256KB, Sector 6-7) */

#define FLASH_END_ADDR          0x0807FFFFU     /* Flash 结束地址 (512KB) */

/* 分区大小 (两个分区取较小值) */
#define PARTITION_SIZE          0x34000U        /* 208KB, 统一可用大小 (Sector 3-5) */

/*
 * STM32F407VETx Flash Layout (512KB)
 *
 * Sector 0 (16KB): Bootloader Code (0x08000000 - 0x08003FFF) ← 不能擦除!
 * Sector 1 (16KB): Bootloader Code (0x08004000 - 0x08007FFF) ← 不能擦除!
 * Sector 2 (16KB): Control Data  (0x08008000 - 0x0800BFFF) ← 不能擦除!
 * Sector 3-5 (208KB): Partition A (0x0800C000 - 0x0803FFFF) ← OTA 写入
 * Sector 6-7 (256KB): Partition B (0x08040000 - 0x0807FFFF) ← OTA 写入
 */

/* Sector 2: 控制数据 (Bootloader 代码执行完后才擦除) */
#define BOOT_CONTROL_MAGIC      0x424F4F54U     /* "BOOT" - Bootloader 有效标志 */
#define BOOT_CONTROL_MAGIC_ADDR 0x0800BFF0U     /* Boot 魔数地址 (Sector 2 末尾) */
#define APP_ACTIVE_ADDR         0x0800BFECU     /* 当前活动分区地址 (4字节) */
#define APP_CRC_ADDR            0x0800BFE8U     /* 当前 App CRC (4字节) */
#define APP_PREV_CRC_ADDR       0x0800BFE4U     /* 上一个 App CRC (4字节) */
#define ROLLBACK_FLAG_ADDR      0x0800BFE0U     /* 回滚标志地址 */
#define ROLLBACK_FLAG_MAGIC     0x524F4C42U     /* "ROLB" */
#define APP_SIZE_ADDR           0x0800BFD8U     /* 固件大小 (4字节) */
#define BOOT_VERSION_ADDR       0x0800BFD0U     /* Bootloader 版本地址 */
#define ERROR_LOG_ADDR          0x0800BF80U     /* 错误日志起始地址 */
#define ERROR_LOG_SIZE          4               /* 最近 4 次错误记录 (Sector 2 安全范围) */
#define ERROR_LOG_ENTRY_SIZE    sizeof(ErrorLogEntry_t)  /* 使用 sizeof 保持与 struct 一致 */

/* 超时定义 */
#define BOOT_TIMEOUT_MS         5000            /* 等待命令超时 (5秒) */

/* UART 定义 */
#define BOOT_UART               USART1
#define BOOT_BAUDRATE           115200

/* 固件块大小 */
#define FIRMWARE_CHUNK_SIZE     128             /* 每次接收 128 字节 */

/* LED 定义 - 使用 test 项目的 LED */
#define BOOT_LED_PORT           GPIOB
#define BOOT_LED_PIN            GPIO_PIN_2

/* 版本定义 */
#define BOOT_VERSION_MAJOR      1
#define BOOT_VERSION_MINOR      0
#define BOOT_VERSION_PATCH      0
#define BOOT_BUILD_DATE         0x20260720      /* 2026-07-20 */

/* Exported functions prototypes ---------------------------------------------*/

/**
  * @brief  初始化 Bootloader
  * @retval None
  */
void Bootloader_Init(void);

/**
  * @brief  运行 Bootloader 主循环
  * @retval None
  */
void Bootloader_Run(void);

/**
  * @brief  检查是否应该进入 Bootloader
  * @retval true=进入 Bootloader, false=跳转到 App
  */
bool Bootloader_ShouldEnter(void);

/**
  * @brief  跳转到 Application
  * @retval None
  */
void Bootloader_JumpToApp(void);

/**
  * @brief  获取当前活动分区地址
  * @retval 活动分区起始地址
  */
uint32_t Bootloader_GetActivePartition(void);

/**
  * @brief  获取目标分区地址 (OTA 写入目标)
  * @retval 目标分区起始地址
  */
uint32_t Bootloader_GetTargetPartition(void);

/**
  * @brief  切换活动分区
  * @retval None
  */
void Bootloader_SwitchPartition(void);

/**
  * @brief  擦除 Application 区域
  * @retval HAL 状态
  */
HAL_StatusTypeDef Bootloader_EraseApp(void);

/**
  * @brief  写入固件数据到目标分区
  * @param  addr: 偏移地址（相对于目标分区起始）
  * @param  data: 数据指针
  * @param  len: 数据长度
  * @retval HAL 状态
  */
HAL_StatusTypeDef Bootloader_WriteFirmware(uint32_t addr, const uint8_t *data, uint32_t len);

/**
  * @brief  校验固件 CRC32
  * @param  expected_crc: 期望的 CRC32 值
  * @retval true=校验通过, false=校验失败
  */
bool Bootloader_VerifyCRC(uint32_t expected_crc);

/**
  * @brief  计算 CRC32
  * @param  data: 数据指针
  * @param  len: 数据长度
  * @retval CRC32 值
  */
uint32_t Bootloader_CalculateCRC(const uint8_t *data, uint32_t len);

/**
  * @brief  设置回退标志
  * @retval None
  */
void Bootloader_SetRollbackFlag(void);

/**
  * @brief  清除回退标志
  * @retval None
  */
void Bootloader_ClearRollbackFlag(void);

/**
  * @brief  检查回退标志
  * @retval true=需要回滚, false=不需要
  */
bool Bootloader_ShouldRollback(void);

/**
  * @brief  记录错误
  * @param  error: 错误代码
  * @retval None
  */
void Bootloader_LogError(BootError_t error);

/**
  * @brief  获取最近错误
  * @retval 最近错误代码
  */
BootError_t Bootloader_GetLastError(void);

/**
  * @brief  发送响应
  * @param  msg: 响应字符串
  * @retval None
  */
void Bootloader_SendResponse(const char *msg);

/**
  * @brief  发送进度
  * @param  current: 当前进度
  * @param  total: 总量
  * @retval None
  */
void Bootloader_SendProgress(uint32_t current, uint32_t total);

/**
  * @brief  设置 Boot 魔数
  * @retval None
  */
void Bootloader_SetMagic(void);

/**
  * @brief  清除 Boot 魔数
  * @retval None
  */
void Bootloader_ClearMagic(void);

/**
  * @brief  获取 Bootloader 版本
  * @retval 版本指针
  */
const BootVersion_t* Bootloader_GetVersion(void);

/**
  * @brief  LED 指示状态
  * @param  state: 状态
  * @retval None
  */
void Bootloader_LED_Set(BootState_t state);

/* 新增：错误恢复策略 */
typedef enum {
    RECOVERY_NONE = 0,
    RECOVERY_RETRY,
    RECOVERY_SKIP,
    RECOVERY_ROLLBACK,
    RECOVERY_MANUAL
} RecoveryAction_t;

/* 新增：OTA 状态枚举 */
typedef enum {
    OTA_STATE_IDLE = 0,
    OTA_STATE_ERASING,
    OTA_STATE_WRITING,
    OTA_STATE_VERIFYING,
    OTA_STATE_COMPLETE,
    OTA_STATE_ERROR,
    OTA_STATE_ROLLBACK
} OTA_State_t;

/* 新增：OTA 控制块 (扩展) - 保留用于未来掉电恢复增强 */
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
/* OTA_RECEIVED_SIZE_ADDR must be 4-byte aligned for uint32_t* access on Cortex-M4.
 * 0x0800BFC8 is word-aligned and within Sector 2 control data area. */
#define OTA_RECEIVED_SIZE_ADDR      0x0800BFC8U

/* 新增：恢复策略函数 */
RecoveryAction_t Bootloader_GetRecoveryAction(BootError_t error, uint8_t retry_count);
bool Bootloader_ExecuteRecovery(RecoveryAction_t action);

/* 新增：掉电恢复函数 */
bool Bootloader_IsPowerLossRecovery(void);
void Bootloader_HandlePowerLoss(void);

#ifdef __cplusplus
}
#endif

#endif /* __BOOTLOADER_H */
