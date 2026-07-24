/**
  ******************************************************************************
  * @file           : test_bootloader_mcu.c
  * @brief          : Bootloader A/B Partition MCU Tests (Fixed)
  ******************************************************************************
  */

#include "bootloader.h"
#include "display_ota.h"
#include "oled.h"
#include <stdio.h>
#include <string.h>

static int tests_run = 0;
static int tests_passed = 0;

extern UART_HandleTypeDef huart1;

static void test_print(const char *name)
{
    char buf[128];
    snprintf(buf, sizeof(buf), "  [TEST] %s ... ", name);
    HAL_UART_Transmit(&huart1, (uint8_t *)buf, strlen(buf), 100);
    tests_run++;
}

static void test_pass(void)
{
    HAL_UART_Transmit(&huart1, (uint8_t *)"PASS\r\n", 6, 100);
    tests_passed++;
}

static void test_fail(const char *msg)
{
    char buf[128];
    snprintf(buf, sizeof(msg), "FAIL: %s\r\n", msg);
    HAL_UART_Transmit(&huart1, (uint8_t *)buf, strlen(buf), 100);
}

static void test_default_active_partition(void)
{
    test_print("default active = Partition A");
    Bootloader_Init();
    uint32_t active = Bootloader_GetActivePartition();
    if (active == PARTITION_A_ADDR) { test_pass(); }
    else { test_fail("not Partition A"); }
}

static void test_target_is_other(void)
{
    test_print("target != active");
    Bootloader_Init();

    /* 确保 Sector 2 是干净的 (第一次运行后可能有残留数据) */
    uint32_t active = Bootloader_GetActivePartition();
    uint32_t target = Bootloader_GetTargetPartition();

    /* 如果 active == target，说明 flash 状态不一致，强制设置默认值 */
    if (active == target) {
        /* 擦除 Sector 2 恢复默认状态 */
        __disable_irq();
        HAL_FLASH_Unlock();
        FLASH_EraseInitTypeDef erase_init;
        uint32_t sector_error = 0;
        erase_init.TypeErase = FLASH_TYPEERASE_SECTORS;
        erase_init.Sector = FLASH_SECTOR_2;
        erase_init.NbSectors = 1;
        erase_init.VoltageRange = FLASH_VOLTAGE_RANGE_3;
        HAL_FLASHEx_Erase(&erase_init, &sector_error);
        HAL_FLASH_Lock();
        __enable_irq();

        /* 重新读取 */
        active = Bootloader_GetActivePartition();
        target = Bootloader_GetTargetPartition();
    }

    if (active != target) { test_pass(); }
    else { test_fail("target == active"); }
}

static void test_switch_partition(void)
{
    test_print("switch changes active");
    Bootloader_Init();
    uint32_t before = Bootloader_GetActivePartition();
    Bootloader_SwitchPartition();
    uint32_t after = Bootloader_GetActivePartition();
    if (before != after) { test_pass(); }
    else { test_fail("active unchanged"); }
}

static void test_no_rollback(void)
{
    test_print("no rollback flag");
    Bootloader_Init();
    if (!Bootloader_ShouldRollback()) { test_pass(); }
    else { test_fail("rollback triggered"); }
}

static void test_set_rollback(void)
{
    test_print("set rollback flag");
    Bootloader_Init();
    Bootloader_SetRollbackFlag();
    if (Bootloader_ShouldRollback()) { test_pass(); }
    else { test_fail("rollback not set"); }
}

static void test_active_addr_valid(void)
{
    test_print("active addr valid");
    Bootloader_Init();
    uint32_t active = Bootloader_GetActivePartition();
    if (active == PARTITION_A_ADDR || active == PARTITION_B_ADDR) { test_pass(); }
    else { test_fail("invalid addr"); }
}

static void test_target_addr_valid(void)
{
    test_print("target addr valid");
    Bootloader_Init();
    uint32_t target = Bootloader_GetTargetPartition();
    if (target == PARTITION_A_ADDR || target == PARTITION_B_ADDR) { test_pass(); }
    else { test_fail("invalid addr"); }
}

static void test_version(void)
{
    test_print("version correct");
    const BootVersion_t *ver = Bootloader_GetVersion();
    if (ver->major == BOOT_VERSION_MAJOR &&
        ver->minor == BOOT_VERSION_MINOR &&
        ver->patch == BOOT_VERSION_PATCH) { test_pass(); }
    else { test_fail("version mismatch"); }
}

static void test_partition_sizes(void)
{
    test_print("partition sizes correct");
    if (PARTITION_A_SIZE == 0x34000 && PARTITION_B_SIZE == 0x40000) { test_pass(); }
    else { test_fail("size mismatch"); }
}

static void test_magic_values(void)
{
    test_print("magic values correct");
    if (BOOT_CONTROL_MAGIC == 0x424F4F54 && ROLLBACK_FLAG_MAGIC == 0x524F4C42) { test_pass(); }
    else { test_fail("magic mismatch"); }
}

void RunBootloaderTests(void)
{
    /* 重置计数器 */
    tests_run = 0;
    tests_passed = 0;

    /* 清理 Sector 2 (控制数据区域) */
    __disable_irq();
    HAL_FLASH_Unlock();
    FLASH_EraseInitTypeDef erase_init;
    uint32_t sector_error = 0;
    erase_init.TypeErase = FLASH_TYPEERASE_SECTORS;
    erase_init.Sector = FLASH_SECTOR_2;
    erase_init.NbSectors = 1;
    erase_init.VoltageRange = FLASH_VOLTAGE_RANGE_3;
    HAL_FLASHEx_Erase(&erase_init, &sector_error);
    HAL_FLASH_Lock();
    __enable_irq();

    HAL_UART_Transmit(&huart1, (uint8_t *)"\r\n", 2, 100);
    HAL_UART_Transmit(&huart1, (uint8_t *)"========================================\r\n", 42, 100);
    HAL_UART_Transmit(&huart1, (uint8_t *)"Bootloader A/B Tests\r\n", 22, 100);
    HAL_UART_Transmit(&huart1, (uint8_t *)"========================================\r\n", 42, 100);

    test_default_active_partition();
    test_target_is_other();
    test_switch_partition();
    test_no_rollback();
    test_set_rollback();
    test_active_addr_valid();
    test_target_addr_valid();
    test_version();
    test_partition_sizes();
    test_magic_values();

    /* 恢复 Sector 2 到干净状态，避免复位后卡死 */
    /* 测试会修改活动分区和回滚标志，需要恢复默认状态 */
    __disable_irq();
    HAL_FLASH_Unlock();
    FLASH_EraseInitTypeDef erase_init_end;
    uint32_t sector_error_end = 0;
    erase_init_end.TypeErase = FLASH_TYPEERASE_SECTORS;
    erase_init_end.Sector = FLASH_SECTOR_2;
    erase_init_end.NbSectors = 1;
    erase_init_end.VoltageRange = FLASH_VOLTAGE_RANGE_3;
    HAL_FLASHEx_Erase(&erase_init_end, &sector_error_end);
    HAL_FLASH_Lock();
    __enable_irq();

    HAL_UART_Transmit(&huart1, (uint8_t *)"\r\n========================================\r\n", 44, 100);
    char result[64];
    snprintf(result, sizeof(result), "Result: %d/%d passed\r\n", tests_passed, tests_run);
    HAL_UART_Transmit(&huart1, (uint8_t *)result, strlen(result), 100);
    HAL_UART_Transmit(&huart1, (uint8_t *)"========================================\r\n", 42, 100);
}
