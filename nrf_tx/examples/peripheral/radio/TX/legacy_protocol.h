/**
 * @file legacy_protocol.h
 * @brief TYGD31 原始 2.4 GHz 图像链路协议常量与字节序辅助函数。
 * @version 1.0.0
 *
 * 本文件定义了摄像头发送板、专用接收板、STM32 主控三端共同遵守的
 * 协议常量。所有改动必须三端同步，否则会出现解包错误或丢帧。
 */

#ifndef LEGACY_PROTOCOL_H
#define LEGACY_PROTOCOL_H

#include <stdint.h>



/* ============================================================
 * 协议长度常量（单位：字节）
 * ============================================================ */

/** nRF RADIO 物理层固定有效载荷长度；两端 RADIO_PACKET_SIZE 必须一致。 */
#define LEGACY_RADIO_PACKET_SIZE             254u

/** 图像 DATA 包中命令字、图片 ID、序列号和分片编号所占用的固定头部长度。 */
#define LEGACY_IMAGE_PACKET_HEADER_SIZE       12u

/** 单个图像 DATA 包能够携带的最大 JPEG 载荷字节数。 */
#define LEGACY_IMAGE_PACKET_PAYLOAD_SIZE     242u

/** 原始协议允许接收的最大 JPEG 字节数（与 image.c 中的合法性校验保持一致）。 */
#define LEGACY_IMAGE_MAX_SIZE              20000u

/** 胶囊序列号字节长度。 */
#define LEGACY_CAPSULE_SN_SIZE                 8u

/** 转发给 STM32 主控时附加的设备信息区长度。 */
#define LEGACY_DEVICE_INFO_SIZE              128u

/** STM32 主控期望的图像帧固定头部长度（含 FF 55 12 34 标识与长度字段）。 */
#define LEGACY_STM_FRAME_HEADER_SIZE            7u



/* ============================================================
 * 命令字（CMD）
 * ============================================================ */

/** 图像开始包：发送板通知接收板一帧图像即将开始。 */
#define LEGACY_CMD_IMAGE_BEGIN               0x01u

/** 图像结束包：发送板通知接收板一帧图像数据已发完。 */
#define LEGACY_CMD_IMAGE_END                 0x03u

/** 胶囊序列号广播：发送板定时广播胶囊 SN，便于接收板识别设备。 */
#define LEGACY_CMD_CAPSULE_SN_BROADCAST      0x05u

/** 胶囊 ID 号广播：发送板广播芯片出厂 DEVICEID，便于接收板/PC 端识别。 */
#define LEGACY_CMD_CAPSULE_ID_BROADCAST      0x07u

/** 图像接收应答：接收板通知发送板某帧图像已成功接收。 */
#define LEGACY_CMD_IMAGE_RECEIVED_RESPONSE   0x10u

/** 图像数据包：携带一段 JPEG 数据的分片。 */
#define LEGACY_CMD_IMAGE_DATA                0x80u

/** 图像转发命令：接收板将完整图像转发给 STM32 主控，再由 STM32 透传到 PC。 */
#define LEGACY_CMD_IMAGE_FORWARD             0x81u



/* ============================================================
 * 字节序辅助函数
 * ============================================================ */

/**
 * @brief 从协议缓冲区读取大端 16 位整数。
 * @param source 至少包含两个字节的输入缓冲区。
 * @return 解析后的 16 位无符号整数。
 * @note 可重入；调用者负责保证 source 非空且长度充足。
 */
static inline uint16_t LegacyProtocol_GetU16Be(const uint8_t *source)
{
    return (uint16_t)(((uint16_t)source[0] << 8) | source[1]);   // 高字节在前：source[0] 为高字节，source[1] 为低字节
}

/**
 * @brief 向协议缓冲区写入大端 16 位整数。
 * @param destination 至少包含两个字节的输出缓冲区。
 * @param value 待写入数值。
 * @return None.
 * @note 可重入；调用者负责保证 destination 非空且长度充足。
 */
static inline void LegacyProtocol_PutU16Be(uint8_t *destination, uint16_t value)
{
    destination[0] = (uint8_t)(value >> 8);                      // 高字节写入 buffer 前部
    destination[1] = (uint8_t)value;                             // 低字节写入 buffer 后部
}

#endif /* LEGACY_PROTOCOL_H */

