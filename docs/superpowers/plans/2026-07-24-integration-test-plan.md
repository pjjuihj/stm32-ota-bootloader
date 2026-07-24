# Integration Test Plan - Bootloader Error Handling

**Date:** 2026-07-24
**Target:** STM32F407VETx
**Prerequisites:** Code compiled with Keil MDK-ARM, flashed to hardware

---

## Test Environment

- **MCU:** STM32F407VETx (LQFP100)
- **Crystal:** 8MHz HSE
- **UART:** USART1, 115200 baud, 8N1
- **I2C:** I2C1 (PB6/PB7), 100kHz
- **OLED:** SSD1306 128x64, I2C address 0x3C (7-bit) / 0x78 (8-bit)
- **LED:** PB2 (active low, Pull-up)
- **Boot Button:** PA0 (active high, Pull-down)
- **Tools:** Serial terminal (e.g., PuTTY, minicom), multimeter, optional I2C logic analyzer

---

## Pre-Test Verification

### P1: Compilation Check
1. Open project in Keil MDK-ARM
2. Build All (F7)
3. Verify: 0 errors, 0 warnings (or only known warnings)
4. **Known issue fixed:** `ErrorLogEntry_t` duplicate definition between `bootloader.h` and `error_log.h` -- resolved by removing the definition from `bootloader.h` and using `error_log.h`'s version

### P2: Flash Layout Verification
After flashing, connect serial terminal and send `info` command:
```
info
```
Expected:
- Active: 0x0800C000 (Partition A) or 0x08040000 (Partition B)
- Target: the other partition
- Rollback: None

---

## Test Cases

### TC-01: Normal Boot (No OTA)

**Setup:** Fresh flash, no boot magic set.

**Steps:**
1. Power on the board
2. Observe OLED: "=== Bootloader ===" then "Jumping to app..."
3. Observe serial output: debug messages about SP, Reset vector, CRC check
4. If valid app exists: board jumps to application
5. If no valid app: board stays in bootloader, LED blinks slowly

**Expected:**
- OLED shows boot sequence
- Serial outputs debug information
- LED blinks slowly in bootloader mode (500ms toggle)
- No crash, no watchdog reset

---

### TC-02: Enter Bootloader via Button

**Steps:**
1. Hold PA0 button while powering on
2. Observe OLED: "Button pressed!"
3. Release button
4. Observe OLED: OTA Update main screen with progress bar
5. Observe serial: "Entering bootloader mode (button)..."

**Expected:**
- Button detection works
- OLED displays OTA interface
- Bootloader enters command processing loop

---

### TC-03: Enter Bootloader via Command

**Steps:**
1. Power on (normal boot, no button)
2. Send via serial: `ota_enter`
3. Send: `reset`
4. Board resets and enters bootloader

**Expected:**
- `ota_enter` returns "OK:Boot mode set"
- `reset` triggers NVIC_SystemReset
- On reboot, bootloader detects magic and enters boot mode

---

### TC-04: Normal OTA Update Flow

**Prerequisites:** Board in bootloader mode, valid firmware hex file ready.

**Steps:**
1. Send: `ota_start <size>` (e.g., `ota_start 4436`)
   - Expected: "OK:Start", progress 0%
2. Send firmware data in chunks: `ota_data <hex>` (128 bytes per chunk)
   - Expected: Progress updates on OLED and serial
   - Example: `ota_data 01020304...` (256 hex chars for 128 bytes)
3. Send: `ota_end <crc32>` (e.g., `ota_end 12345678`)
   - Expected: "Verifying...", then "OK:Verify passed"
4. Observe OLED: "SUCCESS" screen, 3-second countdown
5. Board jumps to new application

**Expected:**
- Flash write completes without errors
- CRC verification passes
- Control data updated (active partition switched)
- OLED shows success with countdown
- New application runs after jump

---

### TC-05: OTA CRC Mismatch

**Steps:**
1. Enter bootloader mode
2. `ota_start 100`
3. Send some data: `ota_data AABBCCDD`
4. `ota_end DEADBEEF` (wrong CRC)
5. Observe serial: "ERROR:CRC mismatch"
6. Observe OLED: "ERROR" screen with expected vs calculated CRC

**Expected:**
- CRC mismatch detected
- Error logged to flash
- LED shows error pattern (3 fast blinks, pause)
- OLED displays error details
- Board stays in bootloader (does NOT jump to invalid app)

---

### TC-06: Power Loss Recovery

**Steps:**
1. Enter bootloader mode
2. `ota_start 10000`
3. Send a few data chunks
4. **Disconnect power** (pull USB/power cable)
5. Wait 2 seconds
6. Reconnect power
7. Observe serial output on boot

**Expected:**
- On reboot, bootloader detects OTA_STATE_WRITING in control data
- Prints "Power loss detected, recovering..."
- Erases and rewrites Sector 2 control data
- Prints "Power loss recovery done"
- Returns to normal bootloader mode
- Previous application remains intact (active partition unchanged)

---

### TC-07: Flash Driver Retry Mechanism

**Setup:** This is best verified via serial debug output.

**Steps:**
1. Enter bootloader mode
2. `ota_start 256`
3. Send 2 chunks of data
4. Monitor serial for "Flash: Erased sector X OK" messages
5. Send `ota_end` with wrong CRC to trigger error path

**Expected:**
- Flash erase shows "Flash: Erased sector X OK"
- Flash write shows successful word writes with readback verification
- On CRC mismatch, error is logged via Bootloader_LogError

---

### TC-08: OLED Degraded Mode

**Steps:**
1. Disconnect OLED I2C cable (PB6 or PB7)
2. Power on the board
3. Enter bootloader mode (button or command)
4. Observe behavior

**Expected:**
- OLED_InitSafe() retries 3 times, then fails
- Serial output: "OLED_InitSafe: FAILED, entering degraded mode"
- ErrorLog_Add(ERROR_LOG_OLED_FAIL, ...) is called
- Board continues operating (no crash)
- LED still functions normally
- Serial commands still work

**Steps (continued):**
5. Reconnect OLED cable
6. Send `ota_start 100`
7. Disconnect OLED during data transfer
8. After 5 consecutive I2C errors, observe serial: "entering LED_ONLY mode"

**Expected:**
- Automatic degradation after threshold
- System remains functional

---

### TC-09: Error Log Persistence

**Steps:**
1. Enter bootloader mode
2. Trigger several errors (wrong CRC, invalid commands, etc.)
3. Send `ota_status` to see error count
4. Power cycle the board
5. Enter bootloader again
6. Send `ota_status`

**Expected:**
- Error count persists across power cycles (stored in Flash Sector 2)
- Error log entries visible in flash at ERROR_LOG_ADDR (0x0800BF80)

---

### TC-10: A/B Partition Switch

**Steps:**
1. Enter bootloader mode
2. Send: `info` -- note active partition
3. Send: `switch`
4. Send: `info` -- active should have changed
5. Send: `reset`
6. On reboot, verify new active partition

**Expected:**
- Partition switches correctly
- Control data (magic, CRC, size) preserved during switch
- After reset, bootloader reads new active partition

---

### TC-11: Rollback Mechanism

**Steps:**
1. Enter bootloader mode
2. Send: `rollback`
3. Send: `reset`
4. On reboot, observe: "Rollback: switching partition..."
5. Board resets again, now on previous partition
6. Send: `info` -- rollback flag should be cleared

**Expected:**
- Rollback flag set in Sector 2
- On reboot, bootloader detects flag, switches partition, clears flag
- Application from previous partition runs

---

### TC-12: Recovery Strategy

**Steps:**
1. This is verified via code review and serial output observation
2. Trigger flash erase failure (if possible, e.g., by writing to protected sector)
3. Observe: Bootloader_GetRecoveryAction returns appropriate action
4. After 3 retries: RECOVERY_ROLLBACK for flash errors

**Expected:**
- Flash errors -> retry up to 3 times, then rollback
- UART timeout -> manual intervention required
- OLED/I2C errors -> ignored (RECOVERY_NONE)

---

### TC-13: Existing Unit Tests

**Steps:**
1. Enter bootloader mode
2. Send: `test`
3. Observe serial output

**Expected:**
- All 10 tests pass
- Output: "Result: 10/10 passed"

**Note:** The PARTITION_A_SIZE test was fixed from 0x38000 to 0x34000 to match the actual definition.

---

### TC-14: LED Indicator States

**Steps:**
1. Power on -- observe LED behavior
2. Enter bootloader mode -- LED should toggle slowly (500ms)
3. Start OTA (`ota_start`) -- LED should turn on (receiving)
4. During CRC verify -- LED should toggle fast
5. On error -- LED should show 3-blink error pattern

**Expected:**
- Each state has distinct LED behavior
- LED is on PB2, active low (SET = off, RESET = on)

---

### TC-15: Version Command

**Steps:**
1. Send: `version`

**Expected:**
- Response: "Bootloader v1.0.0 (Build: 20260720)"

---

## Automated Test Execution

For the built-in test suite, simply send `test` command after entering bootloader mode. This runs 10 unit tests covering:
- Default active partition (A)
- Target partition != active
- Partition switch
- Rollback flag operations
- Address validation
- Version correctness
- Partition size constants
- Magic value constants

---

## Risk Areas

1. **Sector 2 Erasure**: Every control data update requires erasing Sector 2. Power loss during erase corrupts all control data. Mitigation: power loss recovery on next boot.

2. **OLED I2C Bus Hang**: If I2C bus gets stuck (SDA/SCL held low), OLED wrapper cannot recover. Mitigation: hardware I2C reset or external watchdog.

3. **Watchdog Timeout**: Long flash erase operations (especially Sector 5 at 128KB) may trigger IWDG. Mitigation: feed watchdog before and during erase.

4. **Stack Overflow**: `rx_buffer[4200]` is large. Combined with other stack usage, may exceed stack limits. Monitor with stack canary or linker map analysis.
