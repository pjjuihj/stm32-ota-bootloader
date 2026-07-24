/**
  ******************************************************************************
  * @file           : protocol.h
  * @brief          : 通信协议接口
  ******************************************************************************
  */

#ifndef __PROTOCOL_H
#define __PROTOCOL_H

#ifdef __cplusplus
extern "C" {
#endif

#include "stm32f4xx_hal.h"
#include <stdint.h>
#include <stdbool.h>

/* 协议命令 */
typedef enum {
    PROTO_CMD_OTA_ENTER = 0,
    PROTO_CMD_OTA_START,
    PROTO_CMD_OTA_DATA,
    PROTO_CMD_OTA_END,
    PROTO_CMD_OTA_STATUS,
    PROTO_CMD_RESET,
    PROTO_CMD_VERSION,
    PROTO_CMD_ACK,             // 确认
    PROTO_CMD_NACK,            // 否认，请求重传
    PROTO_CMD_PING             // 心跳检测
} ProtocolCmd_t;

/* NACK 原因 */
typedef enum {
    PROTO_NACK_NONE = 0,
    PROTO_NACK_CRC_ERROR,      // CRC 校验失败
    PROTO_NACK_SEQ_ERROR,      // 序列号错误
    PROTO_NACK_TIMEOUT,        // 超时
    PROTO_NACK_BUFFER_FULL     // 缓冲区满
} ProtocolNackReason_t;

/* 数据包结构 */
typedef struct __attribute__((packed)) {
    uint8_t seq;                // 序列号 (0-255 循环)
    uint8_t cmd;                // 命令类型 (ProtocolCmd_t)
    uint16_t len;               // 数据长度
    uint32_t crc32;             // 包头+CRC (从 seq 到 data)
    uint8_t data[];             // 数据 payload
} ProtocolPacket_t;

/* 协议配置 */
#define PROTO_MAX_DATA_SIZE     256     // 最大数据长度
#define PROTO_PACKET_HEADER_SIZE 8      // 包头大小 (seq+cmd+len+crc32)
#define PROTO_ACK_TIMEOUT_MS    1000    // ACK 等待超时
#define PROTO_MAX_RETRIES       5       // 最大重试次数

/* 协议状态 */
typedef struct {
    uint8_t expected_seq;               // 期望的序列号
    uint32_t received_size;             // 已接收大小
    uint32_t total_size;                // 总大小
    uint8_t retry_count;                // 当前重试次数
    uint32_t last_activity_tick;        // 最后活动时间
} ProtocolState_t;

/* 函数声明 */

/**
  * @brief  初始化协议模块
  * @retval None
  */
void Protocol_Init(void);

/**
  * @brief  发送 ACK
  * @param  seq: 确认的序列号
  * @retval None
  */
void Protocol_SendACK(uint8_t seq);

/**
  * @brief  发送 NACK
  * @param  seq: 否认的序列号
  * @param  reason: NACK 原因
  * @retval None
  */
void Protocol_SendNACK(uint8_t seq, ProtocolNackReason_t reason);

/**
  * @brief  计算 CRC32
  * @param  data: 数据指针
  * @param  len: 数据长度
  * @retval CRC32 值
  */
uint32_t Protocol_CalcCRC32(const uint8_t *data, uint32_t len);

/**
  * @brief  发送数据包 (带 ACK 等待)
  * @param  cmd: 命令类型
  * @param  data: 数据指针
  * @param  len: 数据长度
  * @retval true=成功, false=失败
  */
bool Protocol_SendPacket(ProtocolCmd_t cmd, const uint8_t *data, uint16_t len);

/**
  * @brief  等待响应 (带超时)
  * @param  timeout_ms: 超时时间
  * @retval 收到的命令类型, 超时返回 -1
  */
int16_t Protocol_WaitResponse(uint32_t timeout_ms);

/**
  * @brief  检查超时
  * @retval true=超时, false=未超时
  */
bool Protocol_CheckTimeout(void);

/**
  * @brief  获取协议状态
  * @retval ProtocolState_t 指针
  */
ProtocolState_t* Protocol_GetState(void);

#ifdef __cplusplus
}
#endif

#endif /* __PROTOCOL_H */
