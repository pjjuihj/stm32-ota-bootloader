/**
  ******************************************************************************
  * @file           : mock_hal.h
  * @brief          : STM32 HAL Mock 头文件 (PC 端测试用)
  ******************************************************************************
  */

#ifndef __MOCK_HAL_H
#define __MOCK_HAL_H

#include <stdint.h>
#include <stdbool.h>
#include <string.h>

/* ============================================================================
 * 类型定义 (模拟 STM32 HAL 类型)
 * ============================================================================ */

typedef int HAL_StatusTypeDef;
typedef int GPIO_PinState;

#define HAL_OK 0
#define HAL_ERROR 1

#define GPIO_PIN_0  ((uint16_t)0x0001)
#define GPIO_PIN_1  ((uint16_t)0x0002)
#define GPIO_PIN_2  ((uint16_t)0x0004)
#define GPIO_PIN_3  ((uint16_t)0x0008)
#define GPIO_PIN_4  ((uint16_t)0x0010)
#define GPIO_PIN_5  ((uint16_t)0x0020)
#define GPIO_PIN_6  ((uint16_t)0x0040)
#define GPIO_PIN_7  ((uint16_t)0x0080)
#define GPIO_PIN_8  ((uint16_t)0x0100)
#define GPIO_PIN_9  ((uint16_t)0x0200)
#define GPIO_PIN_10 ((uint16_t)0x0400)
#define GPIO_PIN_13 ((uint16_t)0x2000)
#define GPIO_PIN_14 ((uint16_t)0x4000)

#define GPIO_PIN_SET   1
#define GPIO_PIN_RESET 0

#define FLASH_TYPEPROGRAM_WORD 2
#define FLASH_VOLTAGE_RANGE_3  2
#define FLASH_SECTOR_2  2
#define FLASH_SECTOR_6  6

typedef struct {
    uint32_t TypeErase;
    uint32_t Sector;
    uint32_t NbSectors;
    uint32_t VoltageRange;
} FLASH_EraseInitTypeDef;

typedef struct {
    void *Instance;
    struct {
        uint32_t SR;
        uint32_t DR;
    } *Reg;
} UART_HandleTypeDef;

typedef struct {
    uint32_t Pin;
} GPIO_TypeDef;

typedef struct {
    uint16_t SR;
    uint16_t DR;
} USART_TypeDef;

/* ============================================================================
 * Mock Flash 内存
 * ============================================================================ */

#define MOCK_FLASH_SIZE  (512 * 1024)
static uint8_t mock_flash[MOCK_FLASH_SIZE];

void mock_flash_init(void)
{
    memset(mock_flash, 0xFF, MOCK_FLASH_SIZE);
}

uint32_t mock_flash_read32(uint32_t addr)
{
    uint32_t offset = addr - 0x08000000;
    if (offset + 4 <= MOCK_FLASH_SIZE) {
        uint32_t val;
        memcpy(&val, &mock_flash[offset], 4);
        return val;
    }
    return 0xFFFFFFFF;
}

void mock_flash_write32(uint32_t addr, uint32_t val)
{
    uint32_t offset = addr - 0x08000000;
    if (offset + 4 <= MOCK_FLASH_SIZE) {
        memcpy(&mock_flash[offset], &val, 4);
    }
}

void mock_flash_erase_sector(uint32_t sector, uint32_t count)
{
    /* 简化：直接清零对应区域 */
    uint32_t start_addr = 0x08000000;
    uint32_t sector_size = 0;

    /* STM32F407 扇区大小 */
    static const uint32_t sector_sizes[] = {
        16*1024, 16*1024, 16*1024, 16*1024,  /* Sector 0-3: 16KB each */
        64*1024,                              /* Sector 4: 64KB */
        128*1024, 128*1024, 128*1024          /* Sector 5-7: 128KB each */
    };

    for (uint32_t i = 0; i < sector; i++) {
        start_addr += sector_sizes[i];
    }

    uint32_t total_size = 0;
    for (uint32_t i = sector; i < sector + count && i < 8; i++) {
        total_size += sector_sizes[i];
    }

    uint32_t offset = start_addr - 0x08000000;
    if (offset + total_size <= MOCK_FLASH_SIZE) {
        memset(&mock_flash[offset], 0xFF, total_size);
    }
}

/* ============================================================================
 * Mock HAL 函数
 * ============================================================================ */

HAL_StatusTypeDef HAL_FLASH_Unlock(void) { return HAL_OK; }
HAL_StatusTypeDef HAL_FLASH_Lock(void) { return HAL_OK; }

HAL_StatusTypeDef HAL_FLASH_Program(uint32_t type, uint32_t addr, uint32_t data)
{
    if (type == FLASH_TYPEPROGRAM_WORD) {
        mock_flash_write32(addr, data);
    }
    return HAL_OK;
}

HAL_StatusTypeDef HAL_FLASHEx_Erase(FLASH_EraseInitTypeDef *init, uint32_t *error)
{
    mock_flash_erase_sector(init->Sector, init->NbSectors);
    *error = 0;
    return HAL_OK;
}

void __disable_irq(void) {}
void __enable_irq(void) {}
void __set_MSP(uint32_t msp) { (void)msp; }

#define SCB_VTOR *((volatile uint32_t *)0xE000ED08)

/* ============================================================================
 * Mock UART (不实际发送)
 * ============================================================================ */

extern UART_HandleTypeDef huart1;

static char mock_uart_tx_buf[1024];
static uint32_t mock_uart_tx_len = 0;

void mock_uart_reset(void)
{
    mock_uart_tx_len = 0;
    memset(mock_uart_tx_buf, 0, sizeof(mock_uart_tx_buf));
}

HAL_StatusTypeDef HAL_UART_Transmit(UART_HandleTypeDef *huart, uint8_t *data, uint32_t len, uint32_t timeout)
{
    (void)huart;
    (void)timeout;
    if (mock_uart_tx_len + len < sizeof(mock_uart_tx_buf)) {
        memcpy(&mock_uart_tx_buf[mock_uart_tx_len], data, len);
        mock_uart_tx_len += len;
    }
    return HAL_OK;
}

HAL_StatusTypeDef HAL_UART_DeInit(UART_HandleTypeDef *huart) { (void)huart; return HAL_OK; }

uint32_t HAL_GetTick(void) { return 0; }
void HAL_Delay(uint32_t delay) { (void)delay; }

void NVIC_SystemReset(void) { /* 不实际复位 */ }

#endif /* __MOCK_HAL_H */
