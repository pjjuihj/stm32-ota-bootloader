/**
  ******************************************************************************
  * @file           : led_indicator.c
  * @brief          : LED 指示增强实现
  ******************************************************************************
  */

#include "led_indicator.h"

/* 静态变量 */
static LED_State_t current_state = LED_STATE_OFF;
static uint32_t last_toggle_tick = 0;
static uint8_t error_blink_count = 0;
static uint8_t error_blink_target = 0;

/* 初始化 */
void LED_Indicator_Init(void)
{
    current_state = LED_STATE_OFF;
    last_toggle_tick = HAL_GetTick();
    error_blink_count = 0;

    /* 确保 LED 熄灭 (高电平熄灭) */
    HAL_GPIO_WritePin(LED_PORT, LED_PIN, GPIO_PIN_SET);
}

/* 设置 LED 状态 */
void LED_SetState(LED_State_t state)
{
    current_state = state;
    last_toggle_tick = HAL_GetTick();
    error_blink_count = 0;

    switch (state) {
        case LED_STATE_OFF:
            HAL_GPIO_WritePin(LED_PORT, LED_PIN, GPIO_PIN_SET);
            break;
        case LED_STATE_ON:
            HAL_GPIO_WritePin(LED_PORT, LED_PIN, GPIO_PIN_RESET);
            break;
        default:
            break;
    }
}

/* LED 任务 */
void LED_Task(void)
{
    uint32_t now = HAL_GetTick();

    switch (current_state) {
        case LED_STATE_OFF:
            HAL_GPIO_WritePin(LED_PORT, LED_PIN, GPIO_PIN_SET);
            break;

        case LED_STATE_ON:
            HAL_GPIO_WritePin(LED_PORT, LED_PIN, GPIO_PIN_RESET);
            break;

        case LED_STATE_BLINK_SLOW:
            if ((now - last_toggle_tick) >= LED_BLINK_SLOW_MS) {
                HAL_GPIO_TogglePin(LED_PORT, LED_PIN);
                last_toggle_tick = now;
            }
            break;

        case LED_STATE_BLINK_FAST:
            if ((now - last_toggle_tick) >= LED_BLINK_FAST_MS) {
                HAL_GPIO_TogglePin(LED_PORT, LED_PIN);
                last_toggle_tick = now;
            }
            break;

        case LED_STATE_BLINK_ERROR:
            if (error_blink_target == 0) {
                /* 未启动错误闪烁 */
                break;
            }

            if (error_blink_count < error_blink_target * 2) {
                /* 闪烁阶段 */
                if ((now - last_toggle_tick) >= LED_BLINK_FAST_MS) {
                    HAL_GPIO_TogglePin(LED_PORT, LED_PIN);
                    last_toggle_tick = now;
                    error_blink_count++;
                }
            } else {
                /* 暂停阶段 */
                if ((now - last_toggle_tick) >= LED_ERROR_PAUSE_MS) {
                    error_blink_count = 0;
                    last_toggle_tick = now;
                }
            }
            break;

        case LED_STATE_BLINK_PROGRESS:
            /* 进度闪烁，由外部控制 */
            if ((now - last_toggle_tick) >= LED_BLINK_FAST_MS) {
                HAL_GPIO_TogglePin(LED_PORT, LED_PIN);
                last_toggle_tick = now;
            }
            break;
    }
}

/* LED 错误指示 */
void LED_ShowError(uint8_t error_code)
{
    error_blink_target = error_code;
    error_blink_count = 0;
    current_state = LED_STATE_BLINK_ERROR;
    last_toggle_tick = HAL_GetTick();
}

/* 获取当前 LED 状态 */
LED_State_t LED_GetState(void)
{
    return current_state;
}
