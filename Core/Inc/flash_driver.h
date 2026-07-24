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
