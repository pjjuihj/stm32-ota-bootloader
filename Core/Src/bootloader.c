/**
  ******************************************************************************
  * @file           : bootloader.c
  * @brief          : Bootloader 实现
  ******************************************************************************
  * @attention
  *
  * UART Bootloader，用于接收固件并写入 Flash
  * 支持完整备份和回滚功能
  *
  * 功能说明：
  * 1. 通过 UART 接收固件数据
  * 2. 解析 HEX 格式并写入 Flash
  * 3. CRC32 校验确保数据完整性
  * 4. 支持 A/B 分区切换
  * 5. 支持回滚机制
  *
  * 命令协议：
  * - ota_enter: 进入 Boot 模式
  * - ota_start <size>: 开始 OTA 升级
  * - ota_data <hex>: 发送固件数据
  * - ota_end <crc>: 结束 OTA 升级
  * - ota_status: 查看状态
  * - version: 查看版本
  * - reset: 复位设备
  *
  ******************************************************************************
  */

/* Includes ------------------------------------------------------------------*/
#include "bootloader.h"
#include "display_ota.h"
#include "oled.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

/* Private typedef -----------------------------------------------------------*/

/* Private define ------------------------------------------------------------*/

/* Private macro -------------------------------------------------------------*/

/* Private variables ---------------------------------------------------------*/

/**
 * Bootloader 配置结构体
 * 存储当前 OTA 状态、固件大小、CRC 等信息
 */
static BootConfig_t boot_config;

/**
 * UART 句柄 - 使用 main.c 中的 huart1
 * 配置: 115200 波特率, 8N1
 */
extern UART_HandleTypeDef huart1;

/**
 * 命令接收缓冲区
 * 用于存储完整的 UART 命令字符串
 * 大小: 4200 字节 (足够存储最大的 HEX 命令)
 * 计算: 128字节数据 * 2(HEX编码) + 命令前缀 + 余量
 */
static uint8_t rx_buffer[4200];
static uint16_t rx_index = 0;

/**
 * UART 中断接收环形缓冲区
 * 用于在 Flash 写入期间缓存接收到的 UART 数据
 * 大小: 16384 字节
 * 作用: 防止 Flash 写入时 UART 数据丢失
 */
#define UART_RX_BUF_SIZE 16384
static volatile uint8_t uart_rx_buf[UART_RX_BUF_SIZE];
static volatile uint16_t uart_rx_head = 0;  // 写入位置
static volatile uint16_t uart_rx_tail = 0;  // 读取位置
static volatile uint8_t uart_rx_byte;        // 中断接收缓冲

/**
 * 固件数据缓冲区
 * 用于临时存储解析后的固件数据
 */
static uint8_t firmware_chunk[FIRMWARE_CHUNK_SIZE];

/* 版本信息 */
static const BootVersion_t boot_version = {
    .major = BOOT_VERSION_MAJOR,
    .minor = BOOT_VERSION_MINOR,
    .patch = BOOT_VERSION_PATCH,
    .build_date = BOOT_BUILD_DATE
};

/* Private function prototypes -----------------------------------------------*/

static void Bootloader_ProcessCommand(const char *cmd);
static void Bootloader_HandleEnter(const char *param);
static void Bootloader_HandleStart(const char *param);
static void Bootloader_HandleData(const char *param);
static void Bootloader_HandleEnd(const char *param);
static void Bootloader_HandleStatus(const char *param);
static void Bootloader_HandleReset(const char *param);
static void Bootloader_HandleVersion(const char *param);

/**
 * @brief  UART 中断接收回调函数
 * @note   每收到一个字节都会调用此函数
 *         将数据存入环形缓冲区，供主循环处理
 */
void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart) {
    if (huart->Instance == USART1) {
        /* 计算下一个写入位置 */
        uint16_t next_head = (uart_rx_head + 1) % UART_RX_BUF_SIZE;

        /* 如果缓冲区未满，存入数据 */
        if (next_head != uart_rx_tail) {
            uart_rx_buf[uart_rx_head] = uart_rx_byte;
            uart_rx_head = next_head;
        }
        /* 继续接收下一个字节 */
        HAL_UART_Receive_IT(&huart1, &uart_rx_byte, 1);
    }
}

/**
 * @brief  从环形缓冲区读取一个字节
 * @retval 读取的字节，如果缓冲区为空返回 -1
 * @note   非阻塞读取，主循环中调用
 */
static inline int16_t uart_rx_read(void) {
    /* 检查缓冲区是否为空 */
    if (uart_rx_head == uart_rx_tail) return -1;

    /* 读取数据 */
    uint8_t data = uart_rx_buf[uart_rx_tail];
    uart_rx_tail = (uart_rx_tail + 1) % UART_RX_BUF_SIZE;
    return data;
}

/* Private user code ---------------------------------------------------------*/

/* Exported function implementations -----------------------------------------*/

/**
 * @brief  初始化 Bootloader
 * @note   在 main() 中调用，完成以下初始化：
 *         1. 清零配置结构体
 *         2. 初始化环形缓冲区
 *         3. 启动 UART 中断接收
 */
void Bootloader_Init(void)
{
    /* 初始化配置结构体 */
    memset(&boot_config, 0, sizeof(BootConfig_t));
    boot_config.state = BOOT_STATE_IDLE;      /* 初始状态: 空闲 */
    boot_config.max_retries = 3;               /* 最大重试次数 */

    /* 初始化环形缓冲区 */
    uart_rx_head = 0;
    uart_rx_tail = 0;

    /* 启动 UART 中断接收 - 开始监听串口数据 */
    HAL_UART_Receive_IT(&huart1, &uart_rx_byte, 1);

    /* UART、LED、IWDG 已在 main.c 中初始化 */

    /* 发送启动信息 */
    Bootloader_SendResponse("=== Bootloader v1.0.0 ===");
    Bootloader_SendResponse("Waiting for command...");
}

/**
 * @brief  Bootloader 主循环
 * @note   此函数永不返回，持续运行直到跳转到 APP
 *         主要功能：
 *         1. 喂狗防止复位
 *         2. LED 闪烁指示运行状态
 *         3. 从环形缓冲区读取 UART 数据
 *         4. 解析并执行命令
 */
void Bootloader_Run(void)
{
    uint32_t timeout_start = HAL_GetTick();
    uint32_t led_toggle_time = HAL_GetTick();

    while (1) {
        /* 喂狗 - 防止看门狗复位 */
        IWDG->KR = 0xAAAA;

        /* LED 闪烁指示 Bootloader 模式 - 每 500ms 翻转一次 */
        if (HAL_GetTick() - led_toggle_time > 500) {
            HAL_GPIO_TogglePin(BOOT_LED_PORT, BOOT_LED_PIN);
            led_toggle_time = HAL_GetTick();
        }

        /* 从环形缓冲区读取 UART 数据 */
        int16_t byte;
        while ((byte = uart_rx_read()) != -1) {
            timeout_start = HAL_GetTick();  /* 重置超时计时器 */
            boot_config.uart_error_count = 0;

            /* 处理换行符 - 表示一个完整命令 */
            if (byte == '\n' || byte == '\r') {
                if (rx_index > 0) {
                    rx_buffer[rx_index] = '\0';  /* 添加字符串结束符 */
                    Bootloader_ProcessCommand((char *)rx_buffer);  /* 执行命令 */
                    rx_index = 0;  /* 重置索引 */
                }
            } else if (rx_index < sizeof(rx_buffer) - 1) {
                rx_buffer[rx_index++] = byte;  /* 存入缓冲区 */
            } else {
                /* 缓冲区溢出 */
                Bootloader_LogError(BOOT_ERR_UART_OVERFLOW);
                rx_index = 0;
            }
        }

        /* 检查超时 - 重新等待，不跳转 */
        if (boot_config.state == BOOT_STATE_IDLE &&
            (HAL_GetTick() - timeout_start) > BOOT_TIMEOUT_MS) {
            Bootloader_SendResponse("Timeout, waiting for command...");
            timeout_start = HAL_GetTick();
        }
    }
}

uint32_t Bootloader_GetActivePartition(void)
{
    /* 读取活动分区地址 */
    uint32_t *active_addr = (uint32_t *)APP_ACTIVE_ADDR;

    /* 检查是否已设置 */
    if (*active_addr == PARTITION_A_ADDR || *active_addr == PARTITION_B_ADDR) {
        return *active_addr;
    }

    /* 默认返回分区 A */
    return PARTITION_A_ADDR;
}

uint32_t Bootloader_GetTargetPartition(void)
{
    uint32_t active = Bootloader_GetActivePartition();
    return (active == PARTITION_A_ADDR) ? PARTITION_B_ADDR : PARTITION_A_ADDR;
}

void Bootloader_SwitchPartition(void)
{
    uint32_t target = Bootloader_GetTargetPartition();

    /* 检查当前值是否已经是目标值 */
    uint32_t current = *(volatile uint32_t *)APP_ACTIVE_ADDR;
    if (current == target) {
        Bootloader_SendResponse("Already at target partition");
        return;
    }

    /* 备份当前控制数据 */
    uint32_t magic = *(volatile uint32_t *)BOOT_CONTROL_MAGIC_ADDR;
    uint32_t crc = *(volatile uint32_t *)APP_CRC_ADDR;
    uint32_t size = *(volatile uint32_t *)APP_SIZE_ADDR;

    /* 擦除 Sector 2 (控制数据区域，不在 Bootloader 代码区域) */
    __disable_irq();
    HAL_FLASH_Unlock();

    FLASH_EraseInitTypeDef erase_init;
    uint32_t sector_error = 0;
    erase_init.TypeErase = FLASH_TYPEERASE_SECTORS;
    erase_init.Sector = FLASH_SECTOR_2;
    erase_init.NbSectors = 1;
    erase_init.VoltageRange = FLASH_VOLTAGE_RANGE_3;
    HAL_FLASHEx_Erase(&erase_init, &sector_error);

    /* 写入新的活动分区 */
    HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD, APP_ACTIVE_ADDR, target);

    /* 恢复其他控制数据 */
    if (magic == BOOT_CONTROL_MAGIC) {
        HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD, BOOT_CONTROL_MAGIC_ADDR, BOOT_CONTROL_MAGIC);
    }
    /* CRC 和 Size 必须无条件重写 */
    HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD, APP_CRC_ADDR, crc);
    HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD, APP_SIZE_ADDR, size);

    HAL_FLASH_Lock();
    __enable_irq();

    /* 验证 */
    uint32_t verify = *(volatile uint32_t *)APP_ACTIVE_ADDR;
    char msg[64];
    snprintf(msg, sizeof(msg), "Switch OK: 0x%08lX", verify);
    Bootloader_SendResponse(msg);
}

bool Bootloader_ShouldEnter(void)
{
    char msg[128];

    /* 检查 Boot 魔数 */
    uint32_t *magic = (uint32_t *)BOOT_CONTROL_MAGIC_ADDR;
    snprintf(msg, sizeof(msg), "Debug: magic=0x%08lX (expect 0x%08lX)", *magic, BOOT_CONTROL_MAGIC);
    Bootloader_SendResponse(msg);
    if (*magic == BOOT_CONTROL_MAGIC) {
        Bootloader_SendResponse("Debug: Enter due to magic");
        return true;
    }

    /* 检查回滚标志 */
    if (Bootloader_ShouldRollback()) {
        Bootloader_SendResponse("Debug: Enter due to rollback");
        return true;
    }

    /* 获取当前活动分区 */
    uint32_t active_addr = Bootloader_GetActivePartition();
    snprintf(msg, sizeof(msg), "Debug: active=0x%08lX", active_addr);
    Bootloader_SendResponse(msg);

    /* 检查 App 是否有效 */
    uint32_t *app_sp = (uint32_t *)active_addr;
    uint32_t *app_reset = (uint32_t *)(active_addr + 4);

    snprintf(msg, sizeof(msg), "Debug: SP=0x%08lX Reset=0x%08lX", *app_sp, *app_reset);
    Bootloader_SendResponse(msg);

    /* 检查栈指针是否在 RAM 范围内 */
    if (*app_sp < 0x20000000 || *app_sp > 0x20020000) {
        Bootloader_SendResponse("Debug: Invalid SP");
        Bootloader_LogError(BOOT_ERR_INVALID_APP);
        return true;
    }

    /* 检查复位向量是否在 Flash 范围内 */
    if (*app_reset < BOOTLOADER_START_ADDR || *app_reset > FLASH_END_ADDR) {
        Bootloader_SendResponse("Debug: Invalid Reset");
        Bootloader_LogError(BOOT_ERR_INVALID_APP);
        return true;
    }

    /* 检查 App CRC */
    uint32_t *stored_crc = (uint32_t *)APP_CRC_ADDR;
    uint32_t *stored_size = (uint32_t *)APP_SIZE_ADDR;

    snprintf(msg, sizeof(msg), "Debug: CRC=0x%08lX Size=%lu", *stored_crc, *stored_size);
    Bootloader_SendResponse(msg);

    /* CRC 和 Size 必须有效，否则不能跳转 */
    if (*stored_crc == 0xFFFFFFFF || *stored_crc == 0) {
        Bootloader_SendResponse("Debug: Invalid CRC");
        Bootloader_LogError(BOOT_ERR_CRC_MISMATCH);
        return true;  // CRC 无效，进入 Bootloader
    }

    if (*stored_size == 0xFFFFFFFF || *stored_size == 0) {
        Bootloader_SendResponse("Debug: Invalid Size");
        Bootloader_LogError(BOOT_ERR_CRC_MISMATCH);
        return true;  // Size 无效，进入 Bootloader
    }

    uint32_t app_size = *stored_size;

    /* 计算 App CRC */
    uint32_t calculated_crc = 0xFFFFFFFF;
    uint8_t *app_data = (uint8_t *)active_addr;

    for (uint32_t i = 0; i < app_size; i++) {
        uint8_t byte = app_data[i];
        calculated_crc ^= byte;
        for (int j = 0; j < 8; j++) {
            if (calculated_crc & 1) {
                calculated_crc = (calculated_crc >> 1) ^ 0xEDB88320;
            } else {
                calculated_crc >>= 1;
            }
        }
    }
    calculated_crc ^= 0xFFFFFFFF;

    snprintf(msg, sizeof(msg), "Debug: calc_CRC=0x%08lX stored_CRC=0x%08lX", calculated_crc, *stored_crc);
    Bootloader_SendResponse(msg);

    if (calculated_crc != *stored_crc) {
        Bootloader_SendResponse("Debug: CRC mismatch");
        Bootloader_LogError(BOOT_ERR_CRC_MISMATCH);
        return true;  // CRC 不匹配，进入 Bootloader
    }

    Bootloader_SendResponse("Debug: All checks passed, jump to APP");
    return false;  // CRC 校验通过，可以跳转
}

/**
 * @brief  跳转到 Application
 * @note   此函数执行以下步骤：
 *         1. 校验 APP 的栈指针和复位向量
 *         2. 关闭所有中断和外设
 *         3. 重置 SysTick
 *         4. 设置栈指针和跳转地址
 *         5. 跳转到 APP 执行
 * @warning 跳转前必须确保 APP 有效，否则会导致系统崩溃
 */
void Bootloader_JumpToApp(void)
{
    typedef void (*pFunction)(void);
    pFunction jump_to_app;

    /* 等待 Flash 操作完成 */
    while (FLASH->SR & FLASH_SR_BSY) {}

    /* 获取活动分区地址 */
    uint32_t app_addr = Bootloader_GetActivePartition();

    char msg[64];
    snprintf(msg, sizeof(msg), "Jumping to 0x%08lX...", app_addr);
    Bootloader_SendResponse(msg);

    /* 校验栈指针是否在 RAM 范围内 (STM32F407: 0x20000000 - 0x20020000) */
    uint32_t app_sp = *(volatile uint32_t *)app_addr;
    if (app_sp < 0x20000000 || app_sp > 0x20020000) {
        Bootloader_SendResponse("ERROR:Invalid SP, stay in bootloader");
        Bootloader_LogError(BOOT_ERR_INVALID_APP);
        return;  /* 跳转失败，留在 Bootloader */
    }

    /* 校验复位向量是否在 Flash 范围内 */
    uint32_t app_reset = *(volatile uint32_t *)(app_addr + 4);
    if (app_reset < BOOTLOADER_START_ADDR || app_reset > FLASH_END_ADDR) {
        Bootloader_SendResponse("ERROR:Invalid Reset, stay in bootloader");
        Bootloader_LogError(BOOT_ERR_INVALID_APP);
        return;  /* 跳转失败，留在 Bootloader */
    }

    /* 关闭所有中断 - 跳转前必须关闭 */
    __disable_irq();

    /* 关闭看门狗 - IWDG 由 LSI 供电，无法关闭，跳转前停止喂狗即可 */

    /* 关闭 UART - 释放串口资源 */
    HAL_UART_DeInit(&huart1);

    /* 关闭 I2C (OLED 使用) - 释放 I2C 资源 */
    HAL_I2C_DeInit(&hi2c1);

    /* 关闭 LED - 跳转前关闭指示灯 */
    HAL_GPIO_WritePin(BOOT_LED_PORT, BOOT_LED_PIN, GPIO_PIN_SET);

    /**
     * 注意：不要调用 HAL_RCC_DeInit()！
     * 原因：App 的 SystemClock_Config() 会自己配置时钟
     *       HAL_RCC_DeInit() 可能导致 App 无法正确初始化时钟
     */

    /* 重置 SysTick - App 会重新配置 */
    SysTick->LOAD = 0;
    SysTick->VAL = 0;
    SysTick->CTRL = 0;

    /* 设置主堆栈指针 - 从 APP 的向量表读取 */
    __set_MSP(app_sp);

    /* 获取复位向量 - 从 APP 的向量表读取 */
    jump_to_app = (pFunction)app_reset;

    /* 重定位中断向量表 - 指向 APP 的向量表 */
    SCB->VTOR = app_addr;

    /* 跳转到应用 - 执行 APP 的复位向量 */
    jump_to_app();
}

HAL_StatusTypeDef Bootloader_EraseApp(void)
{
    HAL_StatusTypeDef status;
    FLASH_EraseInitTypeDef erase_init;
    uint32_t sector_error = 0;

    /* 获取目标分区地址，确定要擦除的扇区 */
    uint32_t target = Bootloader_GetTargetPartition();

    /* 擦除前喂狗 */
    IWDG->KR = 0xAAAA;

    __disable_irq();

    erase_init.TypeErase = FLASH_TYPEERASE_SECTORS;
    erase_init.VoltageRange = FLASH_VOLTAGE_RANGE_3;

    if (target == PARTITION_A_ADDR) {
        /* 擦除 Sector 3-5 (Sector 2 存放控制数据，不能擦除！) */
        erase_init.Sector = FLASH_SECTOR_3;
        erase_init.NbSectors = 3;
    } else {
        /* 擦除 Sector 6-7 */
        erase_init.Sector = FLASH_SECTOR_6;
        erase_init.NbSectors = 2;
    }

    /* 擦除前喂狗 - 防止看门狗超时 */
    IWDG->KR = 0xAAAA;

    HAL_FLASH_Unlock();
    status = HAL_FLASHEx_Erase(&erase_init, &sector_error);
    HAL_FLASH_Lock();

    __enable_irq();

    if (status == HAL_OK) {
        char msg[64];
        snprintf(msg, sizeof(msg), "Erase OK (0x%08lX)", target);
        Bootloader_SendResponse(msg);
    } else {
        char msg[64];
        snprintf(msg, sizeof(msg), "ERROR:Erase failed at sector %lu", sector_error);
        Bootloader_SendResponse(msg);
        Bootloader_LogError(BOOT_ERR_FLASH_ERASE);
    }

    return status;
}

HAL_StatusTypeDef Bootloader_WriteFirmware(uint32_t addr, const uint8_t *data, uint32_t len)
{
    HAL_StatusTypeDef status = HAL_OK;
    uint32_t target = Bootloader_GetTargetPartition();
    uint32_t flash_addr = target + addr;

    /* 检查地址范围 */
    if (flash_addr + len > FLASH_END_ADDR) {
        Bootloader_LogError(BOOT_ERR_FLASH_WRITE);
        return HAL_ERROR;
    }

    /* 调试：打印写入信息 */
    if (addr == 0) {
        char msg[128];
        snprintf(msg, sizeof(msg), "Write: target=0x%08lX addr=%lu flash=0x%08lX len=%lu",
                 target, addr, flash_addr, len);
        Bootloader_SendResponse(msg);

        /* 调试：显示写入前的 Flash 状态 */
        uint8_t *before = (uint8_t *)flash_addr;
        snprintf(msg, sizeof(msg), "Before[0-7]: %02X %02X %02X %02X %02X %02X %02X %02X",
                 before[0], before[1], before[2], before[3],
                 before[4], before[5], before[6], before[7]);
        Bootloader_SendResponse(msg);
    }

    /* 关闭中断，防止写入 Flash 时被打断 */
    __disable_irq();

    HAL_FLASH_Unlock();

    /* 按字写入 (4字节对齐) */
    for (uint32_t i = 0; i < len; i += 4) {
        uint32_t word = 0;  // ← 恢复为 0
        for (int j = 0; j < 4 && (i + j) < len; j++) {
            word |= ((uint32_t)data[i + j] << (j * 8));
        }

        status = HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD, flash_addr + i, word);
        if (status != HAL_OK) {
            char msg[64];
            snprintf(msg, sizeof(msg), "Write FAIL: addr=0x%08lX status=%d", flash_addr + i, status);
            Bootloader_SendResponse(msg);
            Bootloader_LogError(BOOT_ERR_FLASH_WRITE);
            break;
        }

        /* 回读校验 */
        uint32_t readback = *(volatile uint32_t *)(flash_addr + i);
        if (readback != word) {
            char msg[64];
            snprintf(msg, sizeof(msg), "Verify FAIL: addr=0x%08lX wrote=0x%08lX read=0x%08lX",
                     flash_addr + i, word, readback);
            Bootloader_SendResponse(msg);
            Bootloader_LogError(BOOT_ERR_FLASH_WRITE);
            HAL_FLASH_Lock();
            __enable_irq();
            return HAL_ERROR;
        }

        /* 调试：第一个字写入后显示 */
        if (i == 0) {
            char msg[64];
            snprintf(msg, sizeof(msg), "First word: addr=0x%08lX wrote=0x%08lX read=0x%08lX",
                     flash_addr, word, readback);
            Bootloader_SendResponse(msg);
        }
    }

    HAL_FLASH_Lock();

    /* 重新开启中断 */
    __enable_irq();

    return status;
}

bool Bootloader_VerifyCRC(uint32_t expected_crc)
{
    uint32_t calculated_crc = 0xFFFFFFFF;
    uint32_t target = Bootloader_GetTargetPartition();
    uint8_t *app_data = (uint8_t *)target;
    uint32_t size = boot_config.app_size;

    /* 调试：显示 CRC 计算信息 */
    char msg[128];
    snprintf(msg, sizeof(msg), "CRC: calc from 0x%08lX size=%lu", target, size);
    Bootloader_SendResponse(msg);

    /* 调试：显示前8字节数据 */
    snprintf(msg, sizeof(msg), "Data[0-7]: %02X %02X %02X %02X %02X %02X %02X %02X",
             app_data[0], app_data[1], app_data[2], app_data[3],
             app_data[4], app_data[5], app_data[6], app_data[7]);
    Bootloader_SendResponse(msg);

    /* 计算 CRC32 */
    for (uint32_t i = 0; i < size; i++) {
        uint8_t byte = app_data[i];
        calculated_crc ^= byte;
        for (int j = 0; j < 8; j++) {
            if (calculated_crc & 1) {
                calculated_crc = (calculated_crc >> 1) ^ 0xEDB88320;
            } else {
                calculated_crc >>= 1;
            }
        }
    }
    calculated_crc ^= 0xFFFFFFFF;

    boot_config.calculated_crc = calculated_crc;

    /* 调试：显示 CRC 结果 */
    snprintf(msg, sizeof(msg), "CRC: calculated=0x%08lX expected=0x%08lX match=%s",
             calculated_crc, expected_crc, (calculated_crc == expected_crc) ? "YES" : "NO");
    Bootloader_SendResponse(msg);

    return (calculated_crc == expected_crc);
}

uint32_t Bootloader_CalculateCRC(const uint8_t *data, uint32_t len)
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

/* A/B 分区不需要 BackupApp/RestoreApp/IsBackupValid */

void Bootloader_SetRollbackFlag(void)
{
    /* 备份当前控制数据 */
    uint32_t active = Bootloader_GetActivePartition();
    uint32_t magic = *(volatile uint32_t *)BOOT_CONTROL_MAGIC_ADDR;
    uint32_t crc = *(volatile uint32_t *)APP_CRC_ADDR;
    uint32_t size = *(volatile uint32_t *)APP_SIZE_ADDR;

    /* 擦除 Sector 2 */
    __disable_irq();
    HAL_FLASH_Unlock();

    FLASH_EraseInitTypeDef erase_init;
    uint32_t sector_error = 0;
    erase_init.TypeErase = FLASH_TYPEERASE_SECTORS;
    erase_init.Sector = FLASH_SECTOR_2;
    erase_init.NbSectors = 1;
    erase_init.VoltageRange = FLASH_VOLTAGE_RANGE_3;
    HAL_FLASHEx_Erase(&erase_init, &sector_error);

    /* 重写所有控制数据 */
    if (magic == BOOT_CONTROL_MAGIC) {
        HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD, BOOT_CONTROL_MAGIC_ADDR, BOOT_CONTROL_MAGIC);
    }
    HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD, APP_ACTIVE_ADDR, active);
    /* CRC 和 Size 必须无条件重写 */
    HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD, APP_CRC_ADDR, crc);
    HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD, APP_SIZE_ADDR, size);
    HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD, ROLLBACK_FLAG_ADDR, ROLLBACK_FLAG_MAGIC);

    HAL_FLASH_Lock();
    __enable_irq();

    Bootloader_SendResponse("SetRollback OK");
}

void Bootloader_ClearRollbackFlag(void)
{
    /* 检查当前值，只有当回滚标志存在时才需要清除 */
    uint32_t current_flag = *(volatile uint32_t *)ROLLBACK_FLAG_ADDR;
    if (current_flag != ROLLBACK_FLAG_MAGIC) {
        return;  // 已经清除，不需要再擦除 Sector 2
    }

    /* 需要擦除 Sector 2 来清除回滚标志 */
    uint32_t active = *(volatile uint32_t *)APP_ACTIVE_ADDR;
    uint32_t magic = *(volatile uint32_t *)BOOT_CONTROL_MAGIC_ADDR;
    uint32_t crc = *(volatile uint32_t *)APP_CRC_ADDR;
    uint32_t size = *(volatile uint32_t *)APP_SIZE_ADDR;

    __disable_irq();
    HAL_FLASH_Unlock();

    /* 擦除 Sector 2 */
    FLASH_EraseInitTypeDef erase_init;
    uint32_t sector_error = 0;
    erase_init.TypeErase = FLASH_TYPEERASE_SECTORS;
    erase_init.Sector = FLASH_SECTOR_2;
    erase_init.NbSectors = 1;
    erase_init.VoltageRange = FLASH_VOLTAGE_RANGE_3;
    HAL_FLASHEx_Erase(&erase_init, &sector_error);

    /* 重写所有控制数据 (除了 rollback flag) */
    if (magic == BOOT_CONTROL_MAGIC) {
        HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD, BOOT_CONTROL_MAGIC_ADDR, BOOT_CONTROL_MAGIC);
    }
    HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD, APP_ACTIVE_ADDR, active);
    /* CRC 必须无条件重写，不能用条件判断！ */
    HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD, APP_CRC_ADDR, crc);
    /* Size 也必须无条件重写 */
    HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD, APP_SIZE_ADDR, size);

    HAL_FLASH_Lock();
    __enable_irq();
}

bool Bootloader_ShouldRollback(void)
{
    uint32_t *flag = (uint32_t *)ROLLBACK_FLAG_ADDR;
    return (*flag == ROLLBACK_FLAG_MAGIC);
}

void Bootloader_LogError(BootError_t error)
{
    /* 读取当前错误计数 */
    uint32_t *error_count_addr = (uint32_t *)ERROR_LOG_ADDR;
    uint32_t error_count = *error_count_addr;
    if (error_count == 0xFFFFFFFF) {
        error_count = 0;
    }

    /* 计算写入位置 */
    uint32_t log_addr = ERROR_LOG_ADDR + 4 + ((error_count % ERROR_LOG_SIZE) * ERROR_LOG_ENTRY_SIZE);

    /* 写入错误记录 */
    ErrorLogEntry_t entry;
    entry.error_code = error;
    entry.timestamp = HAL_GetTick();
    entry.state = boot_config.state;
    entry.severity = 0;
    entry.retry_count = 0;
    entry.line = 0;

    __disable_irq();
    HAL_FLASH_Unlock();

    uint32_t *entry_words = (uint32_t *)&entry;
    for (uint32_t i = 0; i < sizeof(ErrorLogEntry_t) / 4; i++) {
        HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD, log_addr + (i * 4), entry_words[i]);
    }

    /* 更新错误计数 */
    error_count++;
    HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD, ERROR_LOG_ADDR, error_count);

    HAL_FLASH_Lock();
    __enable_irq();

    boot_config.last_error = error;
    boot_config.error_count++;
}

BootError_t Bootloader_GetLastError(void)
{
    return boot_config.last_error;
}

void Bootloader_SendResponse(const char *msg)
{
    HAL_UART_Transmit(&huart1, (uint8_t *)msg, strlen(msg), 100);
    HAL_UART_Transmit(&huart1, (uint8_t *)"\r\n", 2, 100);
}

void Bootloader_SendProgress(uint32_t current, uint32_t total)
{
    char msg[64];
    uint32_t percent = current * 100 / total;
    snprintf(msg, sizeof(msg), "Progress: %lu%% (%lu/%lu)", percent, current, total);
    Bootloader_SendResponse(msg);
}

void Bootloader_SetMagic(void)
{
    __disable_irq();
    HAL_FLASH_Unlock();
    HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD, BOOT_CONTROL_MAGIC_ADDR, BOOT_CONTROL_MAGIC);
    HAL_FLASH_Lock();
    __enable_irq();
}

void Bootloader_ClearMagic(void)
{
    /* 检查当前值，只有当魔数存在时才需要清除 */
    uint32_t current_magic = *(volatile uint32_t *)BOOT_CONTROL_MAGIC_ADDR;
    if (current_magic != BOOT_CONTROL_MAGIC) {
        return;  // 已经清除，不需要再擦除 Sector 2
    }

    /* 需要擦除 Sector 2 来清除魔数 */
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

    /* 重写其他控制数据（魔数不写） */
    HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD, APP_ACTIVE_ADDR, active);
    /* CRC 和 Size 必须无条件重写！ */
    HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD, APP_CRC_ADDR, crc);
    HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD, APP_SIZE_ADDR, size);

    HAL_FLASH_Lock();
    __enable_irq();
}

const BootVersion_t* Bootloader_GetVersion(void)
{
    return &boot_version;
}

void Bootloader_LED_Set(BootState_t state)
{
    switch (state) {
        case BOOT_STATE_IDLE:
            HAL_GPIO_WritePin(BOOT_LED_PORT, BOOT_LED_PIN, GPIO_PIN_SET);  /* LED off */
            break;
        case BOOT_STATE_RECEIVING:
            HAL_GPIO_WritePin(BOOT_LED_PORT, BOOT_LED_PIN, GPIO_PIN_RESET);  /* LED on */
            break;
        case BOOT_STATE_VERIFYING:
            /* 快速闪烁 */
            HAL_GPIO_TogglePin(BOOT_LED_PORT, BOOT_LED_PIN);
            break;
        case BOOT_STATE_JUMPING:
            HAL_GPIO_WritePin(BOOT_LED_PORT, BOOT_LED_PIN, GPIO_PIN_SET);  /* LED off */
            break;
        case BOOT_STATE_ROLLBACK:
            /* 慢速闪烁 */
            HAL_GPIO_TogglePin(BOOT_LED_PORT, BOOT_LED_PIN);
            break;
        case BOOT_STATE_ERROR:
            /* 快速闪烁 */
            for (int i = 0; i < 6; i++) {
                HAL_GPIO_TogglePin(BOOT_LED_PORT, BOOT_LED_PIN);
                HAL_Delay(100);
            }
            break;
    }
}

/* Private function implementations ------------------------------------------*/

static void Bootloader_ProcessCommand(const char *cmd)
{
    /* 调试：打印收到的命令 */
    char msg[64];
    snprintf(msg, sizeof(msg), "CMD: %s", cmd);
    Bootloader_SendResponse(msg);

    /* 解析命令 */
    if (strncmp(cmd, "ota_enter", 9) == 0) {
        Bootloader_HandleEnter(cmd + 9);
    } else if (strncmp(cmd, "ota_start", 9) == 0) {
        Bootloader_HandleStart(cmd + 9);
    } else if (strncmp(cmd, "ota_data", 8) == 0) {
        Bootloader_HandleData(cmd + 8);
    } else if (strncmp(cmd, "ota_end", 7) == 0) {
        Bootloader_HandleEnd(cmd + 7);
    } else if (strncmp(cmd, "ota_status", 10) == 0) {
        Bootloader_HandleStatus(cmd + 10);
    } else if (strncmp(cmd, "reset", 5) == 0) {
        Bootloader_HandleReset(cmd + 5);
    } else if (strncmp(cmd, "version", 7) == 0) {
        Bootloader_HandleVersion(cmd + 7);
    } else if (strncmp(cmd, "rollback", 8) == 0) {
        Bootloader_SetRollbackFlag();
        Bootloader_SendResponse("OK:Rollback flag set, reset to apply");
    } else if (strncmp(cmd, "switch", 6) == 0) {
        Bootloader_SwitchPartition();
        uint32_t active = Bootloader_GetActivePartition();
        char msg[64];
        snprintf(msg, sizeof(msg), "OK:Switched to 0x%08lX, reset to apply", active);
        Bootloader_SendResponse(msg);
    } else if (strncmp(cmd, "info", 4) == 0) {
        uint32_t active = Bootloader_GetActivePartition();
        uint32_t target = Bootloader_GetTargetPartition();
        char msg[128];
        snprintf(msg, sizeof(msg), "Active: 0x%08lX (Partition %c)",
                 active, (active == PARTITION_A_ADDR) ? 'A' : 'B');
        Bootloader_SendResponse(msg);
        snprintf(msg, sizeof(msg), "Target: 0x%08lX (Partition %c)",
                 target, (target == PARTITION_A_ADDR) ? 'A' : 'B');
        Bootloader_SendResponse(msg);
        snprintf(msg, sizeof(msg), "Rollback: %s",
                 Bootloader_ShouldRollback() ? "PENDING" : "None");
        Bootloader_SendResponse(msg);
    } else if (strncmp(cmd, "test", 4) == 0) {
        Bootloader_SendResponse("Running tests...");
        extern void RunBootloaderTests(void);
        RunBootloaderTests();
    } else {
        Bootloader_SendResponse("ERROR:Unknown command");
    }
}

static void Bootloader_HandleEnter(const char *param)
{
    (void)param;
    Bootloader_SetMagic();
    Bootloader_SendResponse("OK:Boot mode set");
    Bootloader_SendResponse("Reset to enter bootloader");
}

static void Bootloader_HandleStart(const char *param)
{
    if (param == NULL || strlen(param) == 0) {
        Bootloader_SendResponse("ERROR:Missing size");
        return;
    }

    uint32_t size = atoi(param);
    if (size == 0 || size > PARTITION_SIZE) {
        Bootloader_SendResponse("ERROR:Invalid size");
        return;
    }

    /* 获取目标分区 */
    uint32_t target = Bootloader_GetTargetPartition();
    char msg[64];
    snprintf(msg, sizeof(msg), "Target partition: 0x%08lX", target);
    Bootloader_SendResponse(msg);

    /* 擦除目标分区 */
    if (Bootloader_EraseApp() != HAL_OK) {
        Bootloader_SendResponse("ERROR:Erase failed");
        return;
    }

    boot_config.app_size = size;
    boot_config.received_size = 0;
    boot_config.state = BOOT_STATE_RECEIVING;

    Bootloader_LED_Set(BOOT_STATE_RECEIVING);
    Bootloader_SendResponse("OK:Start");
    Bootloader_SendProgress(0, size);

    /* 更新 OTA 显示 */
    Display_OTA_SetState(OTA_DISP_RECEIVING);
    Display_OTA_SetProgress(0);
    Display_OTA_SetSize(0, size);
}

/**
 * @brief  处理 ota_data 命令 - 接收固件数据
 * @param  param: HEX 格式的固件数据
 * @note   数据格式: ota_data <hex_string>
 *         例如: ota_data 6007002029020008...
 *         每次最多接收 128 字节数据
 */
static void Bootloader_HandleData(const char *param)
{
    /* 参数检查 */
    if (param == NULL || strlen(param) == 0) {
        Bootloader_SendResponse("ERROR:Missing data");
        return;
    }

    /* 跳过前导空格 */
    const char *hex_start = param;
    while (*hex_start == ' ') hex_start++;

    /* 跳过空格后可能为空 */
    if (strlen(hex_start) == 0) {
        Bootloader_SendResponse("ERROR:Missing data");
        return;
    }

    /* 检查是否处于接收状态 */
    if (boot_config.state != BOOT_STATE_RECEIVING) {
        Bootloader_SendResponse("ERROR:Not in receive mode");
        return;
    }

    /* 解析十六进制数据 */
    uint32_t hex_len = strlen(hex_start);

    /* 检查 hex 字符串长度是否为偶数 */
    if (hex_len % 2 != 0) {
        Bootloader_SendResponse("ERROR:Hex length must be even");
        return;
    }

    uint32_t data_len = hex_len / 2;  /* HEX 字符数 / 2 = 实际字节数 */

    /* 使用静态缓冲区，避免栈溢出 */
    static uint8_t data[FIRMWARE_CHUNK_SIZE];

    /* 限制数据长度，防止缓冲区溢出 */
    if (data_len > sizeof(data)) {
        data_len = sizeof(data);
    }

    /* 解析 HEX 字符串为二进制数据 */
    /* 例如: "6007" -> 0x60, 0x07 */
    const char *hex = hex_start;
    for (uint32_t i = 0; i < data_len; i++) {
        uint8_t high = hex[i * 2];      /* 高位字符 */
        uint8_t low = hex[i * 2 + 1];   /* 低位字符 */

        /* 解析高位字符 */
        if (high >= '0' && high <= '9') high -= '0';
        else if (high >= 'a' && high <= 'f') high = high - 'a' + 10;
        else if (high >= 'A' && high <= 'F') high = high - 'A' + 10;
        else {
            Bootloader_SendResponse("ERROR:Invalid hex");
            return;
        }

        /* 解析低位字符 */
        if (low >= '0' && low <= '9') low -= '0';
        else if (low >= 'a' && low <= 'f') low = low - 'a' + 10;
        else if (low >= 'A' && low <= 'F') low = low - 'A' + 10;
        else {
            Bootloader_SendResponse("ERROR:Invalid hex");
            return;
        }

        data[i] = (high << 4) | low;  /* 合并高低位: 0x60 | 0x07 = 0x67 */
    }

    /* 写入 Flash - 从当前偏移地址开始 */
    if (Bootloader_WriteFirmware(boot_config.received_size, data, data_len) != HAL_OK) {
        Bootloader_SendResponse("ERROR:Write failed");
        boot_config.state = BOOT_STATE_ERROR;
        Bootloader_LED_Set(BOOT_STATE_ERROR);
        return;
    }

    /* 更新已接收大小 */
    boot_config.received_size += data_len;

    /* 发送进度信息 */
    Bootloader_SendProgress(boot_config.received_size, boot_config.app_size);

    /* 更新 OLED 进度条 */
    uint8_t progress = boot_config.received_size * 100 / boot_config.app_size;
    Display_OTA_SetProgress(progress);
    Display_OTA_SetSize(boot_config.received_size, boot_config.app_size);
}

static void Bootloader_HandleEnd(const char *param)
{
    if (param == NULL || strlen(param) == 0) {
        Bootloader_SendResponse("ERROR:Missing CRC");
        return;
    }

    if (boot_config.state != BOOT_STATE_RECEIVING) {
        Bootloader_SendResponse("ERROR:Not in receive mode");
        return;
    }

    /* 解析 CRC32 */
    uint32_t expected_crc = strtoul(param, NULL, 16);

    boot_config.app_crc = expected_crc;
    boot_config.state = BOOT_STATE_VERIFYING;
    Bootloader_LED_Set(BOOT_STATE_VERIFYING);

    /* 校验固件 */
    Bootloader_SendResponse("Verifying...");
    Display_OTA_SetState(OTA_DISP_VERIFYING);

    /* 等待一小段时间，让 LED 闪烁 */
    HAL_Delay(500);

    /* 调试：打印 CRC 信息 */
    char msg[128];
    uint32_t target = Bootloader_GetTargetPartition();
    uint32_t active = Bootloader_GetActivePartition();
    snprintf(msg, sizeof(msg), "Verify: active=0x%08lX target=0x%08lX size=%lu expected=0x%08lX",
             active, target, boot_config.app_size, expected_crc);
    Bootloader_SendResponse(msg);

    /* 调试：读取 Flash 前几个字节 */
    uint8_t *flash_data = (uint8_t *)target;
    snprintf(msg, sizeof(msg), "Flash[0-7]: %02X %02X %02X %02X %02X %02X %02X %02X",
             flash_data[0], flash_data[1], flash_data[2], flash_data[3],
             flash_data[4], flash_data[5], flash_data[6], flash_data[7]);
    Bootloader_SendResponse(msg);

    if (Bootloader_VerifyCRC(expected_crc)) {
        /* 校验通过，一次性完成所有控制数据更新 */
        __disable_irq();
        HAL_FLASH_Unlock();

        /* 擦除 Sector 2 */
        FLASH_EraseInitTypeDef erase_init;
        uint32_t sector_error = 0;
        erase_init.TypeErase = FLASH_TYPEERASE_SECTORS;
        erase_init.Sector = FLASH_SECTOR_2;
        erase_init.NbSectors = 1;
        erase_init.VoltageRange = FLASH_VOLTAGE_RANGE_3;
        if (HAL_FLASHEx_Erase(&erase_init, &sector_error) != HAL_OK) {
            Bootloader_SendResponse("ERROR:Erase Sector 2 failed");
            Bootloader_LogError(BOOT_ERR_FLASH_ERASE);
            HAL_FLASH_Lock();
            __enable_irq();
            boot_config.state = BOOT_STATE_ERROR;
            Bootloader_LED_Set(BOOT_STATE_ERROR);
            return;
        }

        /* 写入所有控制数据（一次完成，避免多次擦除） */
        uint32_t new_active = Bootloader_GetTargetPartition();
        HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD, APP_ACTIVE_ADDR, new_active);
        HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD, APP_CRC_ADDR, boot_config.calculated_crc);
        HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD, APP_SIZE_ADDR, boot_config.app_size);
        /* 不写 Magic 和 Rollback Flag，让它们保持 0xFFFFFFFF */

        HAL_FLASH_Lock();
        __enable_irq();

        /* 回读校验控制数据 */
        uint32_t read_active = *(volatile uint32_t *)APP_ACTIVE_ADDR;
        uint32_t read_crc = *(volatile uint32_t *)APP_CRC_ADDR;
        uint32_t read_size = *(volatile uint32_t *)APP_SIZE_ADDR;

        char msg[128];
        if (read_active != new_active || read_crc != boot_config.calculated_crc || read_size != boot_config.app_size) {
            snprintf(msg, sizeof(msg), "ERROR:Ctrl data verify FAIL active=0x%08lX(Exp 0x%08lX) crc=0x%08lX(Exp 0x%08lX)",
                     read_active, new_active, read_crc, boot_config.calculated_crc);
            Bootloader_SendResponse(msg);
            Bootloader_LogError(BOOT_ERR_FLASH_WRITE);
            boot_config.state = BOOT_STATE_ERROR;
            Bootloader_LED_Set(BOOT_STATE_ERROR);
            return;
        }

        snprintf(msg, sizeof(msg), "OK:Verify passed, active=0x%08lX", new_active);
        Bootloader_SendResponse(msg);

        /* OLED 显示成功界面 */
        OLED_Clear();
        OLED_ShowString(20, 0, (uint8_t *)"=== SUCCESS ===", 8, 1);
        OLED_ShowString(0, 2, (uint8_t *)"OTA Update Done!", 8, 1);

        /* 显示新分区信息 */
        snprintf(msg, sizeof(msg), "Active: 0x%08lX", new_active);
        OLED_ShowString(0, 4, (uint8_t *)msg, 8, 1);

        /* 显示固件大小 */
        snprintf(msg, sizeof(msg), "Size: %lu bytes", boot_config.app_size);
        OLED_ShowString(0, 5, (uint8_t *)msg, 8, 1);

        /* 倒计时提示 */
        OLED_ShowString(0, 7, (uint8_t *)"Resetting in 3s...", 8, 1);
        OLED_Refresh();

        Bootloader_SendResponse("Reset to run application");
        boot_config.state = BOOT_STATE_JUMPING;
        Bootloader_LED_Set(BOOT_STATE_JUMPING);

        /* 倒计时3秒后自动复位 */
        for (int i = 3; i > 0; i--) {
            snprintf(msg, sizeof(msg), "Resetting in %ds...  ", i);
            OLED_ShowString(0, 7, (uint8_t *)msg, 8, 1);
            OLED_Refresh();
            HAL_Delay(1000);
        }

        OLED_Clear();
        OLED_ShowString(10, 3, (uint8_t *)"Starting App...", 8, 1);
        OLED_Refresh();
        HAL_Delay(200);

        /* 直接跳转到 App，不复位 */
        Bootloader_JumpToApp();
    } else {
        Bootloader_SendResponse("ERROR:CRC mismatch");
        Bootloader_LogError(BOOT_ERR_CRC_MISMATCH);
        boot_config.state = BOOT_STATE_ERROR;
        Bootloader_LED_Set(BOOT_STATE_ERROR);

        /* OLED 显示错误界面 */
        OLED_Clear();
        OLED_ShowString(20, 0, (uint8_t *)"=== ERROR ===", 8, 1);
        OLED_ShowString(0, 2, (uint8_t *)"CRC Mismatch!", 8, 1);
        OLED_ShowString(0, 4, (uint8_t *)"Expected:", 8, 1);
        snprintf(msg, sizeof(msg), "0x%08lX", expected_crc);
        OLED_ShowString(0, 5, (uint8_t *)msg, 8, 1);
        OLED_ShowString(0, 6, (uint8_t *)"Calculated:", 8, 1);
        snprintf(msg, sizeof(msg), "0x%08lX", boot_config.calculated_crc);
        OLED_ShowString(0, 7, (uint8_t *)msg, 8, 1);
        OLED_Refresh();
    }
}

static void Bootloader_HandleStatus(const char *param)
{
    (void)param;

    char msg[128];
    snprintf(msg, sizeof(msg), "State: %d, Size: %lu, Received: %lu",
             boot_config.state, boot_config.app_size, boot_config.received_size);
    Bootloader_SendResponse(msg);

    snprintf(msg, sizeof(msg), "CRC: Expected 0x%08lX, Calculated 0x%08lX",
             boot_config.app_crc, boot_config.calculated_crc);
    Bootloader_SendResponse(msg);

    snprintf(msg, sizeof(msg), "Errors: %lu, UART Errors: %lu, Last: %d",
             boot_config.error_count, boot_config.uart_error_count, boot_config.last_error);
    Bootloader_SendResponse(msg);

    uint32_t active = Bootloader_GetActivePartition();
    uint32_t target = Bootloader_GetTargetPartition();
    snprintf(msg, sizeof(msg), "Active: 0x%08lX, Target: 0x%08lX", active, target);
    Bootloader_SendResponse(msg);
}

static void Bootloader_HandleReset(const char *param)
{
    (void)param;
    Bootloader_SendResponse("Resetting...");
    HAL_Delay(100);
    NVIC_SystemReset();
}

static void Bootloader_HandleVersion(const char *param)
{
    (void)param;
    char msg[64];
    snprintf(msg, sizeof(msg), "Bootloader v%d.%d.%d (Build: %08lX)",
             boot_version.major, boot_version.minor, boot_version.patch,
             boot_version.build_date);
    Bootloader_SendResponse(msg);
}
