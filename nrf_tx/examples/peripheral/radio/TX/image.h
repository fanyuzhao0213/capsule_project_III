/**
 * @file image.h
 * @brief 摄像头发送板图像采集、打包、无线发送与重发处理
 *
 * 本文件包含 main.c 中所有图像相关函数、状态结构与中断处理器的声明，
 * 配合 image.c 使用；main.c 仅保留初始化与主循环。
 */

#ifndef TX_IMAGE_H
#define TX_IMAGE_H

#include <stdbool.h>
#include <stdint.h>
#include "cx93510.h"
#include "legacy_protocol.h"



/** @brief 图像发送与重发状态机结构体。 */
typedef struct
{
    bool active;                /** true 表示一帧图像仍有分片尚未发完。 */
    bool awaiting_ack;          /** true 表示结束包已发，正在等待接收端 ACK。 */
    bool legacy_send_begin;     /** true 表示本轮首次发送，需要在 DATA 前连发两次 BEGIN。 */
    bool config_block;          /** true 发送 JPEG 配置块；false 发送 JPEG 图像块。 */
    uint16_t frame_id;          /** 帧编号，每采集成功一帧自增 1。 */
    uint16_t fragment_index;    /** 当前数据块的分片编号。 */
    uint16_t fragment_count;    /** 当前数据块总分片数量。 */
    uint8_t  block_pass_index;  /** 当前数据块已发送的遍数。 */
    uint32_t block_offset;      /** 当前数据块在 CX93510 帧缓冲中的偏移。 */
    uint16_t block_size;        /** 当前数据块的字节数。 */
    uint16_t block_sent;        /** 当前数据块已发送的字节数。 */
    uint32_t next_fragment_ms;  /** 下一分片允许发送的绝对时间戳。 */
    uint8_t  legacy_checksum;   /** 整张图像的 8 位累加和。 */
    uint8_t  retry_count;       /** 已发起的重发次数。 */
    uint32_t ack_deadline_ms;   /** ACK 等待超时时间戳。 */
    cx93510_frame_info_t frame; /** CX93510 给出的帧缓冲信息。 */
} image_tx_t;



/** @brief 全局图像发送状态，仅在主循环与中断之间共享。 */
extern image_tx_t g_image_tx;

/** @brief 当前系统时间戳，单位 ms，由 TIMER1_IRQHandler 累加。 */
extern volatile uint32_t g_time_ms;

/** @brief 图像采集请求标志，由 TIMER1 周期置位，主循环清除。 */
extern volatile bool g_capture_due;

/** @brief 胶囊 8 字节序列号镜像（启动时与 SN 重设后由 capsule_sn_refresh 同步指向 capsule_sn_storage_get_active()）。 */
extern uint8_t g_capsule_sn[LEGACY_CAPSULE_SN_SIZE];



/** @brief 初始化 P0.08 图像发送指示灯。 */
void image_tx_led_init(void);

/** @brief 控制图像发送指示灯亮灭。 */
void image_tx_led_set(bool on);

/** @brief 处理每 IMAGE_PERIOD_MS 一次的图像采集请求。 */
void image_capture_task(void);

/** @brief 非阻塞式图像分片发送状态机，每次最多发送一个分片。 */
void image_tx_service(void);

/** @brief 处理图像 ACK 超时，并按原始协议重发 DATA 与 END。 */
void image_ack_service(void);

/** 每 500 ms 广播一次当前生效胶囊 SN（图像链路空闲时）。 */
void capsule_sn_broadcast_service(void);
void capsule_sn_broadcast_burst(uint8_t repeat_count);

/** @brief 主循环中轮询并处理收到的 Radio 控制包。 */
void radio_rx_process(void);

/** @brief TIMER1 1 ms 周期中断。 */
void TIMER1_IRQHandler(void);

/** @brief RADIO 接收中断。 */
void RADIO_IRQHandler(void);

/** @brief 重新让 g_capsule_sn 指向 capsule_sn_storage_get_active()（启动时和 SN 重设后调用）。 */
void capsule_sn_refresh(void);

/** 打开/关闭上电出厂SN配置窗口。 */
void capsule_sn_config_window_begin(void);
void capsule_sn_config_window_end(void);

/** 配置窗口内收到0x46且Flash写入成功后返回true。 */
bool capsule_sn_config_is_complete(void);

/** 最近一次合法出厂SN配置指令到达时刻，供3秒无活动超时判断。 */
uint32_t capsule_sn_config_last_activity_ms(void);

/** @brief 启动 16 MHz 高频晶振供 Radio 和 CX93510 使用。 */
void image_clock_init(void);

/** @brief 配置摄像头板 Radio 收发链路。 */
void radio_configure_image_link(void);



#endif /* TX_IMAGE_H */
