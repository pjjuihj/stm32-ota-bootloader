/**
  ******************************************************************************
  * @file           : display_ota.h
  * @brief          : OTA 显示模块接口
  ******************************************************************************
  */

#ifndef __DISPLAY_OTA_H
#define __DISPLAY_OTA_H

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"
#include <stdint.h>
#include <stdbool.h>

/* OTA 显示状态 */
typedef enum {
    OTA_DISP_IDLE = 0,
    OTA_DISP_RECEIVING,
    OTA_DISP_VERIFYING,
    OTA_DISP_READY,
    OTA_DISP_ERROR
} OTA_DisplayState_t;

/* 显示布局 */
#define OTA_DISP_TITLE_ROW      0
#define OTA_DISP_STATE_ROW      2
#define OTA_DISP_PROGRESS_ROW   3
#define OTA_DISP_SIZE_ROW       4
#define OTA_DISP_SPEED_ROW      5
#define OTA_DISP_TIME_ROW       6
#define OTA_DISP_HINT_ROW       7
#define OTA_DISP_PROGRESS_WIDTH 20

/* 函数声明 */
void Display_OTA_Init(void);
void Display_OTA_ShowMain(void);
void Display_OTA_SetState(OTA_DisplayState_t state);
void Display_OTA_SetProgress(uint8_t progress);
void Display_OTA_SetSize(uint32_t received, uint32_t total);
void Display_OTA_SetSpeed(uint32_t bytes_per_sec);
void Display_OTA_SetTime(uint32_t seconds);
void Display_OTA_ShowError(const char *msg);
void Display_OTA_Clear(void);

#ifdef __cplusplus
}
#endif

#endif /* __DISPLAY_OTA_H */
