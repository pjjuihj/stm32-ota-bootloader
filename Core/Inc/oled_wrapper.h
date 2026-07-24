/**
  ******************************************************************************
  * @file           : oled_wrapper.h
  * @brief          : OLED wrapper interface
  ******************************************************************************
  */

#ifndef __OLED_WRAPPER_H
#define __OLED_WRAPPER_H

#ifdef __cplusplus
extern "C" {
#endif

#include "stm32f4xx_hal.h"
#include <stdint.h>
#include <stdbool.h>

/* Display error types */
typedef enum {
    DISP_ERR_NONE = 0,
    DISP_ERR_I2C_TIMEOUT,
    DISP_ERR_I2C_NACK,
    DISP_ERR_BUFFER_FULL,
    DISP_ERR_INVALID_PARAM
} DisplayError_t;

/* Degradation modes */
typedef enum {
    DEGRADE_NONE = 0,           /* Normal display */
    DEGRADE_RETRY,              /* Retry I2C */
    DEGRADE_LED_ONLY,           /* LED indicator only */
    DEGRADE_SILENT              /* Silent mode */
} DegradeMode_t;

/* OLED operation result */
typedef struct {
    HAL_StatusTypeDef i2c_status;
    bool display_ok;
    uint8_t error_count;
} OLED_Result_t;

/* Error callback type */
typedef void (*DisplayErrorCallback_t)(DisplayError_t error);

/* Configuration */
#define OLED_MAX_RETRIES        3
#define OLED_I2C_TIMEOUT_MS     100
#define OLED_ERROR_THRESHOLD    5   /* Consecutive error threshold */

/* Function declarations */

/**
  * @brief  Initialize OLED wrapper
  * @retval None
  */
void OLED_Wrapper_Init(void);

/**
  * @brief  Safe OLED initialization with error handling
  * @retval OLED_Result_t Operation result
  */
OLED_Result_t OLED_InitSafe(void);

/**
  * @brief  Safe string display with error handling
  * @param  x: X coordinate
  * @param  y: Y coordinate
  * @param  str: String to display
  * @param  size: Font size
  * @param  mode: Display mode
  * @retval OLED_Result_t Operation result
  */
OLED_Result_t OLED_ShowStringSafe(uint8_t x, uint8_t y, const uint8_t *str,
                                   uint8_t size, uint8_t mode);

/**
  * @brief  Safe display refresh with error handling
  * @retval OLED_Result_t Operation result
  */
OLED_Result_t OLED_RefreshSafe(void);

/**
  * @brief  Check if OLED is healthy
  * @retval true=healthy, false=faulty
  */
bool OLED_IsHealthy(void);

/**
  * @brief  Enter degraded mode
  * @retval None
  */
void OLED_DegradedMode(void);

/**
  * @brief  Set error callback
  * @param  callback: Callback function
  * @retval None
  */
void OLED_SetErrorCallback(DisplayErrorCallback_t callback);

/**
  * @brief  Get current degradation mode
  * @retval DegradeMode_t Current mode
  */
DegradeMode_t OLED_GetDegradeMode(void);

#ifdef __cplusplus
}
#endif

#endif /* __OLED_WRAPPER_H */
