/**
 * @file image.h
 * @brief 专用接收板图像接收、重组与 STM32 转发处理
 *
 * 本文件包含 main.c 中所有图像相关结构、函数与中断处理器的声明，
 * 配合 image.c 使用；main.c 仅保留初始化与主循环。
 */

#ifndef RX_IMAGE_H
#define RX_IMAGE_H

#include <stdbool.h>
#include <stdint.h>
#include "legacy_protocol.h"



/**
 * @brief 原始协议单帧重组状态；仅由主循环访问。
 */
typedef struct
{
    bool active;                                    /** 是否正在接收一帧图像。 */
    uint8_t image_id;                               /** 当前帧 ID。 */
    uint8_t capsule_sn[LEGACY_CAPSULE_SN_SIZE];     /** 当前帧对应的胶囊序列号。 */
    uint16_t image_length;                          /** 整帧图像字节长度。 */
    uint16_t packet_count;                          /** 整帧图像总分片数。 */
    uint8_t version_main;                           /** 当前帧 BEGIN 包中读取的胶囊固件主版本号。 */
    uint8_t version_sub;                            /** 当前帧 BEGIN 包中读取的胶囊固件子版本号。 */
    uint8_t version_test;                           /** 当前帧 BEGIN 包中读取的胶囊固件测试版本号。 */
    uint16_t received_count;                        /** 已收到的分片计数。 */
    uint8_t received_map[(LEGACY_IMAGE_MAX_SIZE +
                          LEGACY_IMAGE_PACKET_PAYLOAD_SIZE - 1u) /
                         LEGACY_IMAGE_PACKET_PAYLOAD_SIZE]; /** 各分片接收标志。 */
    uint8_t image[LEGACY_IMAGE_MAX_SIZE];           /** 重组后的图像数据。 */
} legacy_image_rx_t;



/** @brief 单帧图像重组状态全局实例。 */
extern legacy_image_rx_t g_legacy_image_rx;

/** @brief 已封装 STM32 帧的全局缓冲。 */
extern uint8_t g_stm_frame[LEGACY_STM_FRAME_HEADER_SIZE +
                           LEGACY_DEVICE_INFO_SIZE +
                           LEGACY_IMAGE_MAX_SIZE + 1u];

/** @brief 待发送 STM32 帧的总长度，为 0 表示当前没有待发送帧。 */
extern uint32_t g_stm_frame_length;

/** @brief 当前 STM32 帧已发送偏移。 */
extern uint32_t g_stm_frame_offset;

/** @brief 当前待发送 STM32 帧的图像字节长度。 */
extern uint16_t g_stm_image_length;

/** @brief 当前待发送 STM32 帧的图像 ID。 */
extern uint8_t g_stm_image_id;



/**
 * @brief 解析一个原始 TYGD31 Radio 包并推进图片重组状态机。
 * @param packet 指向固定 254 字节且硬件 CRC 正确的数据包。
 */
void receiver_process_legacy_packet(const uint8_t *packet);

/**
 * @brief 将已校验图片封装为原始 STM32 帧并加入异步 UART 发送队列。
 * @return true 表示成功建立待发送帧；false 表示上一帧仍未发送完。
 */
bool receiver_forward_complete_image(void);

/**
 * @brief 向发送端发送两次原始协议图片接收完成应答。
 * @param image_id 已完整接收的图片编号。
 */
void receiver_send_image_ack(uint8_t image_id);

/**
 * @brief 给 RADIO 设置接收缓冲区并启动/继续下一包接收。
 */
void receiver_radio_arm(void);

/**
 * @brief 配置与摄像头发送板完全一致的无线参数。
 */
void receiver_radio_init(void);

/**
 * @brief 上电依次扫描 12 根天线，并固定选择 CRC 成功率/RSSI 最优的一根。
 * @return 最终选择的天线下标，0 表示 ANT1，11 表示 ANT12。
 */
uint8_t receiver_scan_best_antenna(void);

/**
 * @brief Radio 收包中断。
 */
void RADIO_IRQHandler(void);

/**
 * @brief 从环形队列取出一包并交给协议解析器。
 * @return true 表示处理了一包；false 表示队列为空。
 */
bool receiver_forward_one(void);



#endif /* RX_IMAGE_H */
