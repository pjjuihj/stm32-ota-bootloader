# OTA Bootloader 开发日志

**版本:** v1.0.0
**日期:** 2026-07-23

---

## 1. 开发过程中的错误

### 1.1 PA0 按键电平配置错误

**错误：**
```c
// 错误配置
GPIO_InitStruct.Pull = GPIO_PULLUP;  /* 上拉，按下为低电平 */
if (HAL_GPIO_ReadPin(GPIOA, GPIO_PIN_0) == GPIO_PIN_RESET) {  // 检测低电平
```

**实际硬件：**
- 按键接 VCC (3.3V)
- 按下为高电平
- 松开为低电平

**修复：**
```c
// 正确配置
GPIO_InitStruct.Pull = GPIO_PULLDOWN;  /* 下拉，按下为高电平 */
if (HAL_GPIO_ReadPin(GPIOA, GPIO_PIN_0) == GPIO_PIN_SET) {  // 检测高电平
```

**教训：**
- 不要假设硬件配置
- 先确认引脚电平逻辑
- 这就是 CLAUDE.md 说的 "第一天不写代码，先确认外设"

---

### 1.2 PA0 跳转到自己

**错误：**
```c
// APP 编译地址: 0x08000000
// 跳转目标: 0x08000000
// 结果: 跳转到自己，死循环
```

**原因：**
- APP 和 Bootloader 都在 0x08000000
- 按 PA0 时，APP 跳转到 0x08000000（自己）
- 导致崩溃

**修复：**
```c
// 检查 Bootloader 是否存在
static int check_bootloader_exists(void) {
    uint32_t *magic = (uint32_t *)0x0800BFF0;
    if (*magic == 0x424F4F54) {
        return 1;  // Bootloader 存在
    }
    // 检查向量表
    uint32_t sp = *(volatile uint32_t *)0x08000000;
    uint32_t reset = *(volatile uint32_t *)0x08000004;
    if (sp < 0x20000000 || sp > 0x20020000) return 0;
    if (reset < 0x08000000 || reset > 0x08007FFF) return 0;
    return 1;
}
```

**教训：**
- 跳转前必须检查目标是否有效
- 不要盲目跳转

---

### 1.3 PA0 跳转后 Bootloader 没启动

**错误：**
```c
// 直接跳转到 0x08000000
// Bootloader 没有正常启动
// LED 长亮，OLED 不刷新
```

**原因：**
- APP 没有正确重置外设
- 时钟配置没有重置
- I2C 没有关闭

**修复：**
```c
// 使用复位方式（更可靠）
static void jump_to_bootloader(void) {
    // 1. 设置 Boot 魔数
    HAL_FLASH_Program(BOOT_CONTROL_MAGIC_ADDR, BOOT_CONTROL_MAGIC);
    // 2. 复位设备
    NVIC_SystemReset();
}
```

**教训：**
- 手动跳转不可靠
- 复位方式更安全
- 使用 NVIC_SystemReset() 而不是手动跳转

---

### 1.4 OLED 进度条不更新

**错误：**
```c
// Bootloader_HandleData() 中没有调用 Display_OTA_SetProgress()
// 进度条不更新
```

**原因：**
- 只在 ota_start 时设置进度
- 接收数据时没有更新

**修复：**
```c
// 在 Bootloader_HandleData() 中添加
boot_config.received_size += data_len;
Bootloader_SendProgress(boot_config.received_size, boot_config.app_size);

// 更新 OLED 进度条
uint8_t progress = boot_config.received_size * 100 / boot_config.app_size;
Display_OTA_SetProgress(progress);
Display_OTA_SetSize(boot_config.received_size, boot_config.app_size);
```

**教训：**
- 显示更新要在数据处理中调用
- 不要只在初始化时设置

---

### 1.5 OLED 刷新太慢

**错误：**
```c
// 每次进度更新都调用 OLED_Refresh()
// 刷新整个屏幕，太慢
// 进度条看起来不动
```

**原因：**
- OLED_Refresh() 刷新 128x64 像素
- 每次都发送 1024 字节
- I2C 传输慢

**修复：**
```c
// 只在进度变化时更新
static uint8_t last_progress = 255;
if (progress == last_progress) return;
last_progress = progress;
// ... 更新显示
OLED_Refresh();
```

**教训：**
- 减少不必要的刷新
- 使用脏标记（dirty flag）
- 只更新变化的部分

---

## 2. 反思

### 2.1 开发流程问题

**问题：**
- 没有先确认硬件配置
- 假设了按键电平逻辑
- 没有测试单个模块

**正确流程：**
1. 第一天：确认每个外设单独工作
2. 第二天：打通数据链路
3. 第三天：集成显示

### 2.2 代码质量问题

**问题：**
- 没有边界检查
- 没有错误处理
- 没有防误触机制

**改进：**
- 添加长按检测（2秒）
- 添加 Bootloader 存在性检查
- 添加错误提示

### 2.3 调试效率问题

**问题：**
- 靠猜而不是测量
- 没有添加调试输出
- 测试不充分

**改进：**
- 添加调试输出
- 使用逻辑分析仪
- 逐个测试模块

---

## 3. 最佳实践总结

### 3.1 PA0 按键检测

```c
// 正确的 PA0 检测（高电平有效）
GPIO_InitStruct.Pull = GPIO_PULLDOWN;  // 下拉
if (HAL_GPIO_ReadPin(GPIOA, GPIO_PIN_0) == GPIO_PIN_SET) {  // 检测高
    HAL_Delay(50);  // 消抖
    if (HAL_GPIO_ReadPin(GPIOA, GPIO_PIN_0) == GPIO_PIN_SET) {
        // 长按检测
        uint32_t press_start = HAL_GetTick();
        while (HAL_GPIO_ReadPin(GPIOA, GPIO_PIN_0) == GPIO_PIN_SET) {
            HAL_Delay(10);
            if (HAL_GetTick() - press_start >= 2000) {
                // 长按 2 秒，触发操作
                jump_to_bootloader();
            }
        }
    }
}
```

### 3.2 APP 跳转到 Bootloader

```c
// 正确的跳转方式
static void jump_to_bootloader(void) {
    // 1. 检查 Bootloader 是否存在
    if (!check_bootloader_exists()) {
        uart_send_line("[ERROR] No valid bootloader!");
        return;
    }

    // 2. 设置 Boot 魔数
    HAL_FLASH_Unlock();
    HAL_FLASH_Program(BOOT_CONTROL_MAGIC_ADDR, BOOT_CONTROL_MAGIC);
    HAL_FLASH_Lock();

    // 3. 复位设备
    HAL_Delay(100);
    NVIC_SystemReset();
}
```

### 3.3 OLED 进度显示

```c
// 正确的进度更新
void Display_OTA_SetProgress(uint8_t progress) {
    static uint8_t last_progress = 255;

    // 只在进度变化时更新
    if (progress == last_progress) return;
    last_progress = progress;

    // 更新进度条
    // ...

    // 刷新屏幕
    OLED_Refresh();
}
```

---

## 4. 文件结构

```
D:\test\test1\
├── Core\
│   ├── Inc\
│   │   ├── bootloader.h      # Bootloader 接口
│   │   ├── display_ota.h     # OLED 显示接口
│   │   └── main.h            # 主程序头文件
│   └── Src\
│       ├── bootloader.c      # Bootloader 核心
│       ├── display_ota.c     # OLED 显示实现
│       └── main.c            # 主程序
├── Drivers\
│   └── OLED\                 # OLED 驱动
├── docs\
│   └── ota_development_log.md  # 本文档
└── tests\
    └── ota_test.py           # OTA 测试脚本
```

---

## 5. 下一步计划

1. ✅ PA0 按键检测（长按 2 秒）
2. ✅ OLED 进度显示
3. ⬜ 测试完整 OTA 流程
4. ⬜ 添加回滚功能
5. ⬜ 添加错误恢复机制
6. ⬜ 生产环境测试

---

**文档版本:** v1.0.0
**最后更新:** 2026-07-23
