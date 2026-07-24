/**
  ******************************************************************************
  * @file           : oled_wrapper.c
  * @brief          : OLED wrapper implementation
  ******************************************************************************
  */

#include "oled_wrapper.h"
#include "oled.h"
#include "error_log.h"
#include <stdio.h>
#include <string.h>

/* External variables */
extern UART_HandleTypeDef huart1;
extern I2C_HandleTypeDef hi2c1;

/* Static variables */
static OLED_Result_t oled_result;
static DegradeMode_t degrade_mode = DEGRADE_NONE;
static DisplayErrorCallback_t error_callback = NULL;

/* Private function declarations */
static void OLED_Wrapper_SendDebug(const char *msg);

/* Initialization */
void OLED_Wrapper_Init(void)
{
    memset(&oled_result, 0, sizeof(OLED_Result_t));
    degrade_mode = DEGRADE_NONE;
    OLED_Wrapper_SendDebug("OLED_Wrapper: Initialized");
}

/* Safe OLED initialization */
OLED_Result_t OLED_InitSafe(void)
{
    for (int retry = 0; retry < OLED_MAX_RETRIES; retry++) {
        /* Attempt initialization */
        OLED_Init();

        /* Verify I2C communication */
        uint8_t test_data = 0;
        oled_result.i2c_status = HAL_I2C_Master_Transmit(&hi2c1, 0x78, &test_data, 1, OLED_I2C_TIMEOUT_MS);

        if (oled_result.i2c_status == HAL_OK) {
            oled_result.display_ok = true;
            oled_result.error_count = 0;
            degrade_mode = DEGRADE_NONE;
            OLED_Wrapper_SendDebug("OLED_InitSafe: OK");
            return oled_result;
        }

        oled_result.error_count++;
        char msg[64];
        snprintf(msg, sizeof(msg), "OLED_InitSafe: Retry %d/%d", retry + 1, OLED_MAX_RETRIES);
        OLED_Wrapper_SendDebug(msg);

        HAL_Delay(10);
    }

    /* All retries failed */
    oled_result.display_ok = false;
    degrade_mode = DEGRADE_LED_ONLY;

    DisplayError_t error = DISP_ERR_I2C_TIMEOUT;
    if (error_callback) {
        error_callback(error);
    }

    ErrorLog_Add(ERROR_LOG_OLED_FAIL, ERROR_SEVERITY_ERROR, 0);
    OLED_Wrapper_SendDebug("OLED_InitSafe: FAILED, entering degraded mode");

    return oled_result;
}

/* Safe string display */
OLED_Result_t OLED_ShowStringSafe(uint8_t x, uint8_t y, const uint8_t *str,
                                   uint8_t size, uint8_t mode)
{
    if (degrade_mode == DEGRADE_SILENT) {
        oled_result.display_ok = false;
        return oled_result;
    }

    if (degrade_mode == DEGRADE_LED_ONLY) {
        /* LED indicator only, no OLED operation */
        oled_result.display_ok = false;
        return oled_result;
    }

    /* Normal mode: perform the display operation */
    OLED_ShowString(x, y, (uint8_t *)str, size, mode);

    /* Verify I2C bus is alive after the operation */
    uint8_t probe_data = 0;
    oled_result.i2c_status = HAL_I2C_Master_Transmit(&hi2c1, 0x78, &probe_data, 1, OLED_I2C_TIMEOUT_MS);

    if (oled_result.i2c_status == HAL_OK) {
        oled_result.display_ok = true;
        oled_result.error_count = 0;
        return oled_result;
    }

    /* I2C probe failed - operation likely failed */
    oled_result.display_ok = false;
    oled_result.error_count++;
    if (oled_result.error_count >= OLED_ERROR_THRESHOLD) {
        degrade_mode = DEGRADE_LED_ONLY;
        OLED_Wrapper_SendDebug("OLED_ShowStringSafe: Too many errors, entering LED_ONLY mode");
        ErrorLog_Add(ERROR_LOG_OLED_FAIL, ERROR_SEVERITY_ERROR, 0);
    }

    return oled_result;
}

/* Safe display refresh */
OLED_Result_t OLED_RefreshSafe(void)
{
    if (degrade_mode == DEGRADE_SILENT || degrade_mode == DEGRADE_LED_ONLY) {
        oled_result.display_ok = false;
        return oled_result;
    }

    OLED_Refresh();

    /* Verify I2C bus is alive after the refresh */
    uint8_t probe_data = 0;
    oled_result.i2c_status = HAL_I2C_Master_Transmit(&hi2c1, 0x78, &probe_data, 1, OLED_I2C_TIMEOUT_MS);

    if (oled_result.i2c_status == HAL_OK) {
        oled_result.display_ok = true;
        oled_result.error_count = 0;
        return oled_result;
    }

    /* I2C probe failed after refresh */
    oled_result.display_ok = false;
    oled_result.error_count++;
    if (oled_result.error_count >= OLED_ERROR_THRESHOLD) {
        degrade_mode = DEGRADE_LED_ONLY;
        OLED_Wrapper_SendDebug("OLED_RefreshSafe: Too many errors, entering LED_ONLY mode");
        ErrorLog_Add(ERROR_LOG_OLED_FAIL, ERROR_SEVERITY_ERROR, 0);
    }

    return oled_result;
}

/* Check OLED health */
bool OLED_IsHealthy(void)
{
    return oled_result.display_ok && (degrade_mode == DEGRADE_NONE);
}

/* Enter degraded mode */
void OLED_DegradedMode(void)
{
    degrade_mode = DEGRADE_LED_ONLY;
    OLED_Wrapper_SendDebug("OLED: Entering degraded mode (LED only)");
}

/* Set error callback */
void OLED_SetErrorCallback(DisplayErrorCallback_t callback)
{
    error_callback = callback;
}

/* Get current degradation mode */
DegradeMode_t OLED_GetDegradeMode(void)
{
    return degrade_mode;
}

/* Debug output */
static void OLED_Wrapper_SendDebug(const char *msg)
{
    HAL_UART_Transmit(&huart1, (uint8_t *)msg, strlen(msg), 100);
    HAL_UART_Transmit(&huart1, (uint8_t *)"\r\n", 2, 100);
}
