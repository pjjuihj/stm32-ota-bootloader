/**
  ******************************************************************************
  * @file           : test_bootloader.c
  * @brief          : Bootloader A/B 分区单元测试 (PC 端)
  ******************************************************************************
  * @attention
  *
  * 编译: gcc -o test_bootloader test_bootloader.c ../Core/Src/bootloader.c -I../Core/Inc
  * 运行: ./test_bootloader
  *
  ******************************************************************************
  */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

/* Mock STM32 HAL */
#include "mock_hal.h"

/* 被测代码 */
#include "bootloader.h"

/* 测试计数 */
static int tests_run = 0;
static int tests_passed = 0;

#define TEST(name) \
    do { \
        printf("  [TEST] %s ... ", name); \
        tests_run++; \
    } while(0)

#define PASS() \
    do { \
        printf("PASS\n"); \
        tests_passed++; \
    } while(0)

#define FAIL(msg) \
    do { \
        printf("FAIL: %s\n", msg); \
    } while(0)

/* ============================================================================
 * 测试用例
 * ============================================================================ */

/**
 * @brief 测试 1: 默认活动分区应该是 Partition A
 */
void test_default_active_partition(void)
{
    TEST("默认活动分区应该是 Partition A");

    /* 初始化 Mock Flash */
    mock_flash_init();

    /* 初始化 Bootloader */
    Bootloader_Init();

    /* 获取活动分区 */
    uint32_t active = Bootloader_GetActivePartition();

    /* 验证 */
    if (active == PARTITION_A_ADDR) {
        PASS();
    } else {
        FAIL("默认活动分区不是 Partition A");
    }
}

/**
 * @brief 测试 2: 目标分区应该是另一个分区
 */
void test_target_partition_is_other(void)
{
    TEST("目标分区应该是另一个分区");

    mock_flash_init();
    Bootloader_Init();

    uint32_t active = Bootloader_GetActivePartition();
    uint32_t target = Bootloader_GetTargetPartition();

    if (active != target) {
        PASS();
    } else {
        FAIL("目标分区与活动分区相同");
    }
}

/**
 * @brief 测试 3: 切换分区后活动分区改变
 */
void test_switch_partition(void)
{
    TEST("切换分区后活动分区改变");

    mock_flash_init();
    Bootloader_Init();

    uint32_t before = Bootloader_GetActivePartition();
    Bootloader_SwitchPartition();
    uint32_t after = Bootloader_GetActivePartition();

    if (before != after) {
        PASS();
    } else {
        FAIL("切换后活动分区未改变");
    }
}

/**
 * @brief 测试 4: 连续切换两次回到原分区
 */
void test_switch_twice_returns_original(void)
{
    TEST("连续切换两次回到原分区");

    mock_flash_init();
    Bootloader_Init();

    uint32_t original = Bootloader_GetActivePartition();
    Bootloader_SwitchPartition();
    Bootloader_SwitchPartition();
    uint32_t result = Bootloader_GetActivePartition();

    if (original == result) {
        PASS();
    } else {
        FAIL("切换两次后未回到原分区");
    }
}

/**
 * @brief 测试 5: 无回滚标志时不应触发回滚
 */
void test_no_rollback_flag(void)
{
    TEST("无回滚标志时不应触发回滚");

    mock_flash_init();
    Bootloader_Init();

    if (!Bootloader_ShouldRollback()) {
        PASS();
    } else {
        FAIL("无标志时错误触发回滚");
    }
}

/**
 * @brief 测试 6: 设置回滚标志后应触发回滚
 */
void test_set_rollback_flag(void)
{
    TEST("设置回滚标志后应触发回滚");

    mock_flash_init();
    Bootloader_Init();

    Bootloader_SetRollbackFlag();

    if (Bootloader_ShouldRollback()) {
        PASS();
    } else {
        FAIL("设置标志后未触发回滚");
    }
}

/**
 * @brief 测试 7: 清除回滚标志后不应触发回滚
 */
void test_clear_rollback_flag(void)
{
    TEST("清除回滚标志后不应触发回滚");

    mock_flash_init();
    Bootloader_Init();

    Bootloader_SetRollbackFlag();
    Bootloader_ClearRollbackFlag();

    if (!Bootloader_ShouldRollback()) {
        PASS();
    } else {
        FAIL("清除标志后仍触发回滚");
    }
}

/* ============================================================================
 * 主函数
 * ============================================================================ */

int main(void)
{
    printf("========================================\n");
    printf("Bootloader A/B 分区单元测试\n");
    printf("========================================\n\n");

    /* 运行所有测试 */
    test_default_active_partition();
    test_target_partition_is_other();
    test_switch_partition();
    test_switch_twice_returns_original();
    test_no_rollback_flag();
    test_set_rollback_flag();
    test_clear_rollback_flag();

    /* 输出结果 */
    printf("\n========================================\n");
    printf("结果: %d/%d 通过\n", tests_passed, tests_run);
    printf("========================================\n");

    return (tests_passed == tests_run) ? 0 : 1;
}
