/**
  ******************************************************************************
  * @file           : flash_driver.c
  * @brief          : Flash 操作封装实现
  ******************************************************************************
  */

#include "flash_driver.h"
#include <stdio.h>
#include <string.h>

/* 外部变量 */
extern UART_HandleTypeDef huart1;

/* 私有函数声明 */
static void FlashDriver_SendDebug(const char *msg);

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
            char msg[128];
            snprintf(msg, sizeof(msg), "Flash: Erased sector %lu OK", sector);
            FlashDriver_SendDebug(msg);
            return result;
        }

        if (retry < FLASH_MAX_RETRIES) {
            result.level = FLASH_ERR_LVL_ERROR;
            result.msg = "Erase retry";
            char msg[128];
            snprintf(msg, sizeof(msg), "Flash: Erase sector %lu failed (sector_error=%lu), retry %lu/%d",
                     sector, sector_error, retry + 1, FLASH_MAX_RETRIES);
            FlashDriver_SendDebug(msg);
            HAL_Delay(FLASH_RETRY_DELAY_MS);
        }
    }

    result.level = FLASH_ERR_LVL_FATAL;
    result.msg = "Erase failed after retries";
    char msg[128];
    snprintf(msg, sizeof(msg), "Flash: FATAL - Erase sector %lu failed after max retries (sector_error=%lu)",
             sector, sector_error);
    FlashDriver_SendDebug(msg);
    return result;
}

/* 写入一个字 (带重试和回读校验) */
FlashResult_t FlashDriver_WriteWord(uint32_t addr, uint32_t data)
{
    FlashResult_t result = {0};

    /* 检查地址对齐 */
    if (addr % 4 != 0) {
        result.status = HAL_ERROR;
        result.level = FLASH_ERR_LVL_FATAL;
        result.msg = "Address not 4-byte aligned";
        char msg[128];
        snprintf(msg, sizeof(msg), "Flash: FATAL - WriteWord addr 0x%08lX not 4-byte aligned", addr);
        FlashDriver_SendDebug(msg);
        return result;
    }

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
    char msg[128];
    snprintf(msg, sizeof(msg), "Flash: FATAL - Write 0x%08lX failed", addr);
    FlashDriver_SendDebug(msg);
    return result;
}

/* 写入数据块 */
FlashResult_t FlashDriver_WriteBlock(uint32_t addr, const uint8_t *data, uint32_t len)
{
    FlashResult_t result = {0};

    /* 检查地址和长度对齐 */
    if (addr % 4 != 0 || len % 4 != 0) {
        result.status = HAL_ERROR;
        result.level = FLASH_ERR_LVL_FATAL;
        result.msg = "Address or length not 4-byte aligned";
        char msg[128];
        snprintf(msg, sizeof(msg), "Flash: FATAL - WriteBlock addr 0x%08lX len %lu not 4-byte aligned",
                 addr, len);
        FlashDriver_SendDebug(msg);
        return result;
    }

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
