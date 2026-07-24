/**
  ******************************************************************************
  * @file           : protocol.c
  * @brief          : 通信协议实现
  ******************************************************************************
  */

#include "protocol.h"
#include <stdio.h>
#include <string.h>

/* 外部变量 */
extern UART_HandleTypeDef huart1;

/* 静态变量 */
static ProtocolState_t proto_state;

/* 私有函数声明 */
static void Protocol_SendDebug(const char *msg);

/* 初始化 */
void Protocol_Init(void)
{
    memset(&proto_state, 0, sizeof(ProtocolState_t));
    proto_state.expected_seq = 0;
    proto_state.last_activity_tick = HAL_GetTick();

    Protocol_SendDebug("Protocol: Initialized");
}

/* 发送 ACK */
void Protocol_SendACK(uint8_t seq)
{
    uint8_t data[1] = {seq};
    Protocol_SendPacket(PROTO_CMD_ACK, data, 1);
}

/* 发送 NACK */
void Protocol_SendNACK(uint8_t seq, ProtocolNackReason_t reason)
{
    uint8_t data[2] = {seq, (uint8_t)reason};
    Protocol_SendPacket(PROTO_CMD_NACK, data, 2);
}

/* 计算 CRC32 */
uint32_t Protocol_CalcCRC32(const uint8_t *data, uint32_t len)
{
    uint32_t crc = 0xFFFFFFFF;

    for (uint32_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (int j = 0; j < 8; j++) {
            if (crc & 1) {
                crc = (crc >> 1) ^ 0xEDB88320;
            } else {
                crc >>= 1;
            }
        }
    }

    return crc ^ 0xFFFFFFFF;
}

/* 发送数据包 */
bool Protocol_SendPacket(ProtocolCmd_t cmd, const uint8_t *data, uint16_t len)
{
    if (len > PROTO_MAX_DATA_SIZE) {
        return false;
    }

    /* 构造包头 */
    ProtocolPacket_t packet;
    packet.seq = proto_state.expected_seq;
    packet.cmd = (uint8_t)cmd;
    packet.len = len;

    /* 计算 CRC (不含 crc32 字段本身) */
    uint8_t header_for_crc[6] = {
        packet.seq,
        packet.cmd,
        (uint8_t)(len & 0xFF),
        (uint8_t)(len >> 8)
    };
    packet.crc32 = Protocol_CalcCRC32(header_for_crc, 6);
    if (data && len > 0) {
        packet.crc32 = Protocol_CalcCRC32(data, len) ^ packet.crc32;
    }

    /* 发送包头 */
    HAL_UART_Transmit(&huart1, (uint8_t *)&packet, PROTO_PACKET_HEADER_SIZE, 100);

    /* 发送数据 */
    if (data && len > 0) {
        HAL_UART_Transmit(&huart1, (uint8_t *)data, len, 100);
    }

    return true;
}

/* 等待响应 */
int16_t Protocol_WaitResponse(uint32_t timeout_ms)
{
    uint32_t start = HAL_GetTick();
    uint8_t buffer[PROTO_PACKET_HEADER_SIZE + PROTO_MAX_DATA_SIZE];
    uint16_t index = 0;

    while ((HAL_GetTick() - start) < timeout_ms) {
        /* 尝试接收 */
        uint8_t byte;
        if (HAL_UART_Receive(&huart1, &byte, 1, 10) == HAL_OK) {
            buffer[index++] = byte;

            /* 检查是否收到完整包头 */
            if (index >= PROTO_PACKET_HEADER_SIZE) {
                ProtocolPacket_t *pkt = (ProtocolPacket_t *)buffer;

                /* 检查数据长度 */
                if (index >= PROTO_PACKET_HEADER_SIZE + pkt->len) {
                    /* 收到完整包，返回命令 */
                    return pkt->cmd;
                }
            }
        }
    }

    return -1;  // 超时
}

/* 检查超时 */
bool Protocol_CheckTimeout(void)
{
    return (HAL_GetTick() - proto_state.last_activity_tick) > 30000;  // 30秒
}

/* 获取协议状态 */
ProtocolState_t* Protocol_GetState(void)
{
    return &proto_state;
}

/* 调试输出 */
static void Protocol_SendDebug(const char *msg)
{
    HAL_UART_Transmit(&huart1, (uint8_t *)msg, strlen(msg), 100);
    HAL_UART_Transmit(&huart1, (uint8_t *)"\r\n", 2, 100);
}
