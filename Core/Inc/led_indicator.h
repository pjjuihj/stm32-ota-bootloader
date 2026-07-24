/**
  ******************************************************************************
  * @file           : led_indicator.h
  * @brief          : LED 指示增强接口
  ******************************************************************************
  */

#ifndef __LED指示_H
#define __LED指示_H

#ifdef __cplusplus
extern "C" {
#endif

#include "stm32f4xx_hal.h"
#include <stdint.h>
#include <stdbool.h>

/* LED 状态定义 */
typedef enum {
    LED_STATE_OFF = 0,              // 熄灭
    LED_STATE_ON,                   // 常亮
    LED_STATE_BLINK_SLOW,           // 慢闪 (500ms)
    LED_STATE_BLINK_FAST,           // 快闪 (100ms)
    LED_STATE_BLINK_ERROR,          // 错误闪烁 (3次快闪后暂停)
    LED_STATE_BLINK_PROGRESS        // 进度闪烁
} LED_State_t;

/* LED 配置 */
#define LED_PORT                GPIOB
#define LED_PIN                 GPIO_PIN_2
#define LED_BLINK_SLOW_MS       500
#define LED_BLINK_FAST_MS       100
#define LED_ERROR_PAUSE_MS      1000

/* 函数声明 */

/**
  * @brief  初始化 LED 指示
  * @retval None
  */
void LED_Indicator_Init(void);

/**
  * @brief  设置 LED 状态
  * @param  state: LED 状态
  * @retval None
  */
void LED_SetState(LED_State_t state);

/**
  * @brief  LED 任务 (在主循环中调用)
  * @retval None
  */
void LED_Task(void);

/**
  * @brief  LED 错误指示
  * @param  error_code: 错误代码 (闪烁次数)
  * @retval None
  */
void LED_ShowError(uint8_t error_code);

/**
  * @brief  获取当前 LED 状态
  * @retval LED_State_t 当前状态
  */
LED_State_t LED_GetState(void);

#ifdef __cplusplus
}
#endif

#endif /* __LED指示_H */
