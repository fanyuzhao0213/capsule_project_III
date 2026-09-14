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

/**
 * TX 0x10图像块允许发送的最大原始JPEG字节数。
 *
 * STM32保持TYGD31旧版固定记录：20064 = 128字节DeviceInfo +
 * 19936字节完整JPEG。CX93510的0x10块不含DQT/DHT，STM32会用686字节
 * 标准头替换原始2字节SOI，因此安全上限为19936 - 686 + 2 = 19252。
 * 超过此值的帧必须在TX端拒绝，不得进入Radio。
 */
#define LEGACY_IMAGE_MAX_SIZE              19252u

/** 胶囊序列号字节长度。 */
#define LEGACY_CAPSULE_SN_SIZE                 8u

/** 转发给 STM32 主控时附加的设备信息区长度。 */
#define LEGACY_DEVICE_INFO_SIZE              128u

/** STM32 主控期望的图像帧固定头部长度（含 FF 55 12 34 标识与长度字段）。 */
#define LEGACY_STM_FRAME_HEADER_SIZE            7u

/* 128字节设备信息由TX、RX、STM32逐级补齐；偏移定义必须与RX/STM32一致。 */
#define LEGACY_DEVICE_INFO_MARKER_OFFSET          0u
#define LEGACY_DEVICE_INFO_MARKER_SIZE            5u
#define LEGACY_DEVICE_INFO_TX_VERSION_MAIN_OFFSET 5u
#define LEGACY_DEVICE_INFO_TX_VERSION_SUB_OFFSET  6u
#define LEGACY_DEVICE_INFO_RX_VERSION_MAIN_OFFSET 7u
#define LEGACY_DEVICE_INFO_RX_VERSION_SUB_OFFSET  8u
#define LEGACY_DEVICE_INFO_STM_VERSION_MAIN_OFFSET 9u
#define LEGACY_DEVICE_INFO_STM_VERSION_SUB_OFFSET 10u
#define LEGACY_DEVICE_INFO_CAPSULE_SN_OFFSET      11u
#define LEGACY_DEVICE_INFO_RX_DEVICE_ID_OFFSET    19u
#define LEGACY_DEVICE_INFO_RX_DEVICE_ID_SIZE       8u
#define LEGACY_DEVICE_INFO_STM_DEVICE_ID_OFFSET   27u
#define LEGACY_DEVICE_INFO_STM_DEVICE_ID_SIZE     12u
#define LEGACY_DEVICE_INFO_ANT_RSSI_1_6_OFFSET    39u
#define LEGACY_DEVICE_INFO_STM_UPTIME_OFFSET      45u
#define LEGACY_DEVICE_INFO_ACTIVE_ANTENNA_OFFSET  49u
#define LEGACY_DEVICE_INFO_RADIO_FREQUENCY_OFFSET 50u
#define LEGACY_DEVICE_INFO_ANT_RSSI_7_12_OFFSET   51u
#define LEGACY_DEVICE_INFO_TX_VERSION_TEST_OFFSET 61u
#define LEGACY_DEVICE_INFO_RX_VERSION_TEST_OFFSET 62u
#define LEGACY_DEVICE_INFO_STM_VERSION_TEST_OFFSET 63u
#define LEGACY_DEVICE_INFO_JPEG_LENGTH_OFFSET     65u
#define LEGACY_DEVICE_INFO_RTC_OFFSET             70u
#define LEGACY_DEVICE_INFO_RTC_SIZE                7u
#define LEGACY_DEVICE_INFO_PREVIOUS_RTC_OFFSET    83u

/** BEGIN包中的ADXL362原始数据扩展；旧接收端会把该区域当作Reserved忽略。 */
#define LEGACY_BEGIN_ACCEL_VALID_OFFSET         17u
#define LEGACY_BEGIN_ACCEL_X_OFFSET             18u
#define LEGACY_BEGIN_ACCEL_Y_OFFSET             20u
#define LEGACY_BEGIN_ACCEL_Z_OFFSET             22u

/** RX转发给STM32时，ADXL362扩展在128字节DeviceInfo中的位置。 */
#define LEGACY_DEVICE_INFO_ACCEL_VALID_OFFSET   90u
#define LEGACY_DEVICE_INFO_ACCEL_X_OFFSET       91u
#define LEGACY_DEVICE_INFO_ACCEL_Y_OFFSET       93u
#define LEGACY_DEVICE_INFO_ACCEL_Z_OFFSET       95u



/* ============================================================
 * 命令字（CMD）
 * ============================================================ */

/** 图像开始包：发送板通知接收板一帧图像即将开始。 */
#define LEGACY_CMD_IMAGE_BEGIN               0x01u

/** 图像结束包：发送板通知接收板一帧图像数据已发完。 */
#define LEGACY_CMD_IMAGE_END                 0x03u

/** 胶囊序列号广播：发送板定时广播胶囊 SN，便于接收板识别设备。 */
#define LEGACY_CMD_CAPSULE_SN_BROADCAST      0x05u

/** 图像接收应答：接收板通知发送板某帧图像已成功接收。 */
#define LEGACY_CMD_IMAGE_RECEIVED_RESPONSE   0x10u

/** 图像数据包：携带一段 JPEG 数据的分片。 */
#define LEGACY_CMD_IMAGE_DATA                0x80u

/** 图像转发命令：接收板将完整图像转发给 STM32 主控，再由 STM32 透传到 PC。 */
#define LEGACY_CMD_IMAGE_FORWARD             0x81u

/** PC 端到胶囊端的 ZAYS 控制帧命令。 */
#define LEGACY_CMD_SN_PREPARE_REQUEST         0x40u
#define LEGACY_CMD_SN_PREPARE_RESPONSE        0x41u
#define LEGACY_CMD_DEVICE_ID_QUERY_REQUEST    0x42u
#define LEGACY_CMD_DEVICE_ID_QUERY_RESPONSE   0x43u
#define LEGACY_CMD_SN_SET_REQUEST             0x44u
#define LEGACY_CMD_SN_SET_RESPONSE            0x45u
#define LEGACY_CMD_SN_CONFIRM_REQUEST         0x46u
#define LEGACY_CMD_SN_CONFIRM_RESPONSE        0x47u
#define LEGACY_CMD_CONTROL_ERROR_RESPONSE     0x49u
#define LEGACY_CONTROL_RESULT_OK              0x01u
#define LEGACY_CONTROL_RESULT_ERROR           0x00u
#define LEGACY_CONTROL_FRAME_MIN_SIZE            8u
#define LEGACY_CONTROL_FRAME_MAX_SIZE           32u



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

/** 从协议缓冲区读取大端二补码16位有符号整数。 */
static inline int16_t LegacyProtocol_GetI16Be(const uint8_t *source)
{
    return (int16_t)LegacyProtocol_GetU16Be(source);
}

/** 向协议缓冲区写入大端二补码16位有符号整数。 */
static inline void LegacyProtocol_PutI16Be(uint8_t *destination, int16_t value)
{
    LegacyProtocol_PutU16Be(destination, (uint16_t)value);
}

#endif /* LEGACY_PROTOCOL_H */
