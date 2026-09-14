/**
 * @file config.h
 * @brief 专用接收板应用配置：所有用户可调参数集中在此文件
 *
 * 修改本文件即可调整 Radio 队列、UART 桥接、图像接收策略等。
 * 协议常量（命令字、最大图像长度等）保留在 legacy_protocol.h。
 */

#ifndef RX_CONFIG_H
#define RX_CONFIG_H

#include <stdint.h>
#include "legacy_protocol.h"

/** 1：统计完整图片从Radio BEGIN到UART实际发送完毕的耗时和有效帧率。 */
#define RX_FRAME_RATE_LOG_ENABLED 1u

/** 1：每帧打印RX刚生成、尚未由STM补充的128字节DeviceInfo。 */
#define RX_DEVICE_INFO_LOG_ENABLED 1u



/* ============================================================
 * 软件版本号（接收板固件版本，写入日志/STM 帧显示用）
 * ============================================================ */

/** 接收板固件主版本号。 */
#define VERSION_MAIN               (1)
/** 接收板固件子版本号。 */
#define VERSION_SUB                (1)
/** 接收板固件测试版本号。 */
#define VERSION_TEST               (0)


/* ============================================================
 * Radio 链路参数
 * ============================================================ */

/** Radio 物理层固定有效载荷字节数（与协议包长度一致）。 */
#define RADIO_PACKET_SIZE        LEGACY_RADIO_PACKET_SIZE

/** Radio 工作频率；必须与 TX 保持一致。 */
#define RADIO_FREQUENCY_MHZ      2410u
#define RADIO_FREQUENCY_OFFSET   (RADIO_FREQUENCY_MHZ - 2400u)

/** Radio 接收队列深度。 */
#define RADIO_QUEUE_DEPTH        8u

/** Radio 中断优先级（数值越小优先级越高；本工程无 SoftDevice）。 */
#define RADIO_IRQ_PRIORITY       6u

/** 接收板 ACK 发送功率。 */
#define RECEIVER_ACK_TX_POWER    RADIO_TXPOWER_TXPOWER_Neg8dBm



/* ============================================================
 * RF1662SR 12 路天线开关
 * ============================================================ */

/** RF1662 串行控制时钟，对应原理图 SP12T_SCLK。 */
#define RF1662_SCLK_PIN          19u

/** RF1662 串行控制数据，对应原理图 SP12T_SDATA。 */
#define RF1662_SDATA_PIN         20u

/** 启动时固定选择的天线：0=ANT1，11=ANT12。 */
#define RF1662_DEFAULT_ANTENNA   0u

/** 未绑定发现状态下每根天线驻留650ms，可覆盖一次500ms SN广播。 */
#define RF1662_DISCOVERY_DWELL_MS 650u

/** 发现SN后继续驻留当前天线，保证STM32能收到重复广播。 */
#define RF1662_DISCOVERY_FOUND_HOLD_MS 1500u

/** 绑定目标选优时每根天线的采样时间。 */
#define RF1662_TARGET_SCAN_DWELL_MS   8u

/** 绑定后快速交错扫描完整循环次数。 */
#define RF1662_TARGET_SCAN_ROUNDS     5u

/** 固定接收阶段的分级失联判断。 */
#define RF1662_TARGET_PACKET_TIMEOUT_MS 1500u
#define RF1662_COMPLETE_IMAGE_TIMEOUT_MS 4000u
#define RF1662_FAILED_FRAME_LIMIT         3u

/** 天线管理状态机调度粒度。 */
#define RF1662_SERVICE_TICK_MS       4u

/**
 * 每根天线的扫描驻留时间，大于 TX 的 500 ms 发图周期，确保每路
 * 至少覆盖一段有效无线数据。总启动扫描时间约为本值乘以 12。
 */
#define RF1662_SCAN_DWELL_MS     650u



/* ============================================================
 * UART 桥接参数
 * ============================================================ */

/** UART RX 引脚，接收 STM32 数据。 */
#define UART_RX_PIN              16u

/** UART TX 引脚，向 STM32 发送图像数据。 */
#define UART_TX_PIN              15u

/** UART 中断优先级，数值越小优先级越高，必须高于 Radio 防止 1 Mbps 漏字节。 */
#define UART_IRQ_PRIORITY        5u

/** UART RX 环形缓冲区大小，必须为 2 的幂。 */
#define UART_RX_BUFFER_SIZE      256u

/** UART RX 环形缓冲区索引掩码。 */
#define UART_RX_BUFFER_MASK      (UART_RX_BUFFER_SIZE - 1u)

/** UART TX FIFO 大小。 */
#define UART_TX_FIFO_SIZE        1024u

/** 单次 RTT HEXDUMP 输出的字节数。 */
#define UART_RX_LOG_CHUNK        32u

/** 单次UARTE EasyDMA发送块；nRF52832 MAXCNT为8位，最大255字节。 */
#define UART_TX_DMA_CHUNK_SIZE   255u

/** STM 一包数据结束的判定：最后一个字节后连续无新字节的时长，单位 us。 */
#define UART_RX_IDLE_TIMEOUT_US  100000u

/** UART 单包最大字节数。 */
#define UART_RX_PACKET_MAX_SIZE  1024u

/** TIMER2（UART 静默定时器）中断优先级。 */
#define UART_IDLE_TIMER_IRQ_PRIORITY 7u

/** 接收板外部模式控制脚：接收期间保持低电平，发送 ACK 时短暂拉高。 */
#define RECEIVER_MODE_PIN        23u



#endif /* RX_CONFIG_H */
