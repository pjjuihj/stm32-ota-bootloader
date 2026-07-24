/**
  ******************************************************************************
  * @file           : display_ota.c
  * @brief          : OTA 显示模块实现
  ******************************************************************************
  */

#include "display_ota.h"
#include "oled.h"
#include <stdio.h>
#include <string.h>

/* 当前显示状态 */
static OTA_DisplayState_t current_state = OTA_DISP_IDLE;

/* 显示缓冲区 */
static char disp_buf[32];

/* 进度条宽度 */
#define BAR_WIDTH 20

void Display_OTA_Init(void)
{
    current_state = OTA_DISP_IDLE;
}

void Display_OTA_ShowMain(void)
{
    OLED_Clear();

    /* 标题 - 第0行 */
    const char *title = "=== OTA Update ===";
    uint16_t title_len = strlen(title) * 6;
    uint16_t title_x = (OLED_WIDTH - title_len) / 2;
    OLED_ShowString(title_x, 0, (uint8_t *)title, 8, 1);

    /* 状态 - 第1行 */
    OLED_ShowString(0, 1, (uint8_t *)"Status: Waiting...", 8, 1);

    /* 进度条 - 第2-3行 */
    uint16_t bar_x = 4;
    uint16_t bar_y = 18;
    uint16_t bar_w = OLED_WIDTH - 8;

    /* 进度条边框 */
    OLED_DrawLine(bar_x, bar_y, bar_x + bar_w, bar_y, 1);
    OLED_DrawLine(bar_x, bar_y + 10, bar_x + bar_w, bar_y + 10, 1);
    OLED_DrawLine(bar_x, bar_y, bar_x, bar_y + 10, 1);
    OLED_DrawLine(bar_x + bar_w, bar_y, bar_x + bar_w, bar_y + 10, 1);

    /* 百分比 - 第3行（居中） */
    OLED_ShowString(OLED_WIDTH/2 - 12, 3, (uint8_t *)"0%", 8, 1);

    /* 信息 - 第4-5行 */
    OLED_ShowString(0, 5, (uint8_t *)"Size: 0/0 KB", 8, 1);
    OLED_ShowString(0, 6, (uint8_t *)"Speed: 0 B/s", 8, 1);

    /* 提示 - 第7行 */
    OLED_ShowString(0, 7, (uint8_t *)"Press BTN to cancel", 8, 1);

    OLED_Refresh();
}

void Display_OTA_SetState(OTA_DisplayState_t state)
{
    current_state = state;

    switch (state) {
        case OTA_DISP_IDLE:
            OLED_ShowString(0, 1, (uint8_t *)"Status: Waiting...", 8, 1);
            break;
        case OTA_DISP_RECEIVING:
            OLED_ShowString(0, 1, (uint8_t *)"Status: Receiving", 8, 1);
            break;
        case OTA_DISP_VERIFYING:
            OLED_ShowString(0, 1, (uint8_t *)"Status: Verifying...", 8, 1);
            break;
        case OTA_DISP_READY:
            OLED_ShowString(0, 1, (uint8_t *)"Status: Ready!", 8, 1);
            OLED_ShowString(0, 7, (uint8_t *)"Reset to apply     ", 8, 1);
            break;
        case OTA_DISP_ERROR:
            break;
    }

    OLED_Refresh();
}

void Display_OTA_SetProgress(uint8_t progress)
{
    static uint8_t last_progress = 255;

    /* 只在进度变化时更新 */
    if (progress == last_progress) return;
    last_progress = progress;

    if (progress > 100) progress = 100;

    /* 绘制进度条边框 */
    uint16_t bar_x = 4;
    uint16_t bar_y = 18;
    uint16_t bar_w = OLED_WIDTH - 8;

    /* 清除进度条内部 */
    for (uint16_t x = bar_x + 1; x < bar_x + bar_w - 1; x++) {
        for (uint16_t y = bar_y + 1; y < bar_y + 10; y++) {
            OLED_DrawPoint(x, y, 0);
        }
    }

    /* 填充进度条 */
    uint16_t fill_w = (bar_w - 2) * progress / 100;
    for (uint16_t x = bar_x + 1; x < bar_x + 1 + fill_w; x++) {
        for (uint16_t y = bar_y + 1; y < bar_y + 10; y++) {
            OLED_DrawPoint(x, y, 1);
        }
    }

    /* 百分比（居中显示） */
    snprintf(disp_buf, sizeof(disp_buf), "%d%%", progress);
    uint16_t pct_len = strlen(disp_buf) * 6;
    uint16_t pct_x = (OLED_WIDTH - pct_len) / 2;
    OLED_ShowString(pct_x, 3, (uint8_t *)disp_buf, 8, 1);

    OLED_Refresh();
}

void Display_OTA_SetSize(uint32_t received, uint32_t total)
{
    if (total < 1024) {
        snprintf(disp_buf, sizeof(disp_buf), "Size: %lu/%lu B", received, total);
    } else {
        snprintf(disp_buf, sizeof(disp_buf), "Size: %lu/%lu KB", received / 1024, total / 1024);
    }
    OLED_ShowString(0, 5, (uint8_t *)disp_buf, 8, 1);
    OLED_Refresh();
}

void Display_OTA_SetSpeed(uint32_t bytes_per_sec)
{
    if (bytes_per_sec < 1024) {
        snprintf(disp_buf, sizeof(disp_buf), "Speed: %lu B/s", bytes_per_sec);
    } else {
        snprintf(disp_buf, sizeof(disp_buf), "Speed: %lu.%lu KB/s",
                 bytes_per_sec / 1024, (bytes_per_sec % 1024) * 10 / 1024);
    }
    OLED_ShowString(0, 6, (uint8_t *)disp_buf, 8, 1);
    OLED_Refresh();
}

void Display_OTA_SetTime(uint32_t seconds)
{
    // 暂时不显示时间，节省空间
    // 如果需要，可以替换第6行的 Speed 显示
}

void Display_OTA_ShowError(const char *msg)
{
    current_state = OTA_DISP_ERROR;

    OLED_Clear();

    /* 标题 */
    OLED_ShowString(0, 0, (uint8_t *)"=== OTA Error ===", 8, 1);

    /* 错误信息 */
    if (msg != NULL) {
        OLED_ShowString(0, 2, (uint8_t *)msg, 8, 1);
    } else {
        OLED_ShowString(0, 2, (uint8_t *)"Unknown error", 8, 1);
    }

    /* 提示 */
    OLED_ShowString(0, 4, (uint8_t *)"Press BTN to retry", 8, 1);

    OLED_Refresh();
}

void Display_OTA_Clear(void)
{
    OLED_Clear();
    OLED_Refresh();
}
