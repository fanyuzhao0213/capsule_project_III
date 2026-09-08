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

/** 上电时扫描全部 12 根天线；置 0 时直接使用 RF1662_DEFAULT_ANTENNA。 */
#define RF1662_STARTUP_SCAN_ENABLED 1u

/**
 * 每根天线的扫描驻留时间，必须大于 TX 的 500 ms 发图周期，确保每路
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

/** 每轮最多向 UART FIFO 填入的字节数，防止长时间占用主循环。 */
#define UART_TX_SERVICE_BUDGET   64u

/** STM 一包数据结束的判定：最后一个字节后连续无新字节的时长，单位 us。 */
#define UART_RX_IDLE_TIMEOUT_US  500000u

/** UART 单包最大字节数。 */
#define UART_RX_PACKET_MAX_SIZE  1024u

/** TIMER2（UART 静默定时器）中断优先级。 */
#define UART_IDLE_TIMER_IRQ_PRIORITY 7u

/** 接收板外部模式控制脚：接收期间保持低电平，发送 ACK 时短暂拉高。 */
#define RECEIVER_MODE_PIN        23u

/**
 * 启动时是否通过 UART 发送一次 ASCII 测试串给 STM/PC。
 * 正式产品若不希望出现测试文字，置 0 即可；图像包始终保持纯透传。
 */
#define UART_BRIDGE_STARTUP_TEST 1u



#endif /* RX_CONFIG_H */
