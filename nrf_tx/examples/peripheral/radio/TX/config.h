/**
 * @file config.h
 * @brief 摄像头发送板应用配置：所有用户可调参数集中在此文件
 *
 * 修改本文件即可调整采集周期、图像处理策略、Radio 队列、中断优先级等。
 * 协议常量（命令字、最大图像长度等）保留在 legacy_protocol.h。
 */

#ifndef TX_CONFIG_H
#define TX_CONFIG_H

#include <stdint.h>
#include "legacy_protocol.h"

/* ============================================================
 * RTT日志开关
 * ============================================================ */

/**
 * TX日志总开关：
 * 1：允许初始化NRF_LOG及RTT后端，并按下面的分类开关输出日志；
 * 0：不编译日志输出、不初始化日志后端、不执行日志刷新和后台处理。
 * 正式低功耗固件保持为0，联调时临时改为1。
 */
#define TX_LOG_ENABLED              1u

/** 1：仅输出上电协议参数；不打开逐帧运行日志。 */
#define TX_BOOT_LOG_ENABLED         1u

/** 1：输出SN配置处理日志；仅在TX_LOG_ENABLED=1时生效。 */
#define TX_CONFIG_LOG_ENABLED       0u

/** 1：仅输出SN广播、图片发送和ADXL362采样运行日志。 */
#define TX_RUNTIME_LOG_ENABLED      0u

/** 1：输出摄像头/SPI等初始化日志；仅在TX_LOG_ENABLED=1时生效。 */
#define TX_INIT_LOG_ENABLED         0u

/** 1：每次成功采集图像后输出一次ADXL362 XYZ和姿态日志。 */
#define ADXL362_LOG_ENABLED         0u

/* 总开关关闭时同步关闭Nordic日志前端和RTT后端，减少Flash、RAM和运行功耗。 */
#if !TX_LOG_ENABLED
#ifndef NRF_LOG_ENABLED
#define NRF_LOG_ENABLED             0
#endif
#ifndef NRF_LOG_BACKEND_RTT_ENABLED
#define NRF_LOG_BACKEND_RTT_ENABLED 0
#endif
#endif



/* ============================================================
 * 软件版本号（写入 BEGIN 包 byte 14-16，供接收端/PC 端显示）
 * ============================================================ */

/** 胶囊固件主版本号。 */
#define VERSION_MAIN               (1)

/** 胶囊固件子版本号。 */
#define VERSION_SUB                (1)

/** 胶囊固件测试版本号。 */
#define VERSION_TEST               (0)



/* ============================================================
 * Flash 存储地址（与 TYGD31 一致；后续集成 drv_flash 时使用）
 * ============================================================ */

/** 用户设定的胶囊序列号存储地址（Flash 起始页）。 */
#define FLASH_CAPSULE_SN_ADDR      (1024u * 256u)



/* ============================================================
 * Radio 链路参数
 * ============================================================ */

/** Radio 物理层固定有效载荷字节数（与协议包长度一致）。 */
#define RADIO_PACKET_SIZE        LEGACY_RADIO_PACKET_SIZE

/** Radio 工作频率；nRF52832 FREQUENCY 寄存器保存相对 2400 MHz 的偏移。 */
#define RADIO_FREQUENCY_MHZ      2410u
#define RADIO_FREQUENCY_OFFSET   (RADIO_FREQUENCY_MHZ - 2400u)

/** Radio 接收控制包的队列深度。 */
#define RADIO_QUEUE_DEPTH        4u

/** Radio 中断优先级（数值越小优先级越高；本工程无 SoftDevice）。 */
#define RADIO_IRQ_PRIORITY       6u



/* ============================================================
 * 图像采集与发送策略
 * ============================================================ */

/** 图像采集请求周期，单位 ms。 */
#define IMAGE_PERIOD_MS          500u

/** 当前联调阶段仅广播 SN，不采集、不发送图片；正式测试图片时改为 1。 */
#define IMAGE_TRANSMISSION_ENABLED 1u

/** 胶囊 SN 常态广播周期。 */
#define SN_BROADCAST_PERIOD_MS   500u

/** SN 配置成功后立即重复广播次数。 */
#define SN_BROADCAST_STARTUP_REPEAT 3u

/** 相邻分片发送完成后等待的间隔，单位 ms；兼顾延迟与接收端处理能力。 */
#define IMAGE_FRAGMENT_GAP_MS    1u

/**
 * 图像块重复发送总开关：
 * 0：每个数据块发送一遍（默认，吞吐量最高）；
 * 1：每个数据块发送两遍，第二遍用于补齐第一遍丢失的分片。
 */
#define IMAGE_REPEAT_SEND_ENABLED 0

/** 重发两遍数据块之间的间隔，单位 ms。 */
#define IMAGE_PASS_GAP_MS        20u

#if IMAGE_REPEAT_SEND_ENABLED
#define IMAGE_BLOCK_PASSES       2u
#else
#define IMAGE_BLOCK_PASSES       1u
#endif

/** 单帧图像采集的最大允许阻塞时间，单位 ms。 */
#define IMAGE_CAPTURE_TIMEOUT_MS 1000u

/** 补光灯点亮且传感器唤醒后等待约1帧；沿用旧工程验证过的拍照时序。 */
#define IMAGE_CAPTURE_LIGHT_WARMUP_MS 25u
/** 发送结束后等待接收端 ACK 的超时时间，单位 ms。 */
#define IMAGE_ACK_TIMEOUT_MS     30u
/** ACK 超时后允许重发的最大次数。 */
#define IMAGE_MAX_RETRIES        1u
/** 单个图像数据包能够携带的有效图像字节数。 */
#define IMAGE_PAYLOAD_SIZE       LEGACY_IMAGE_PACKET_PAYLOAD_SIZE



/* ============================================================
 * 看门狗
 * ============================================================ */

/** 看门狗超时时间，单位 s；当前采集最长 1 s，设 3 s 既覆盖正常流程又能异常复位。 */
#define WATCHDOG_TIMEOUT_SECONDS 3u

/** 看门狗时钟源 32.768 kHz。 */
#define WATCHDOG_CLOCK_HZ        32768u

/** 看门狗重装载计数值 = 超时秒数 × 时钟频率。 */
#define WATCHDOG_RELOAD_TICKS    (WATCHDOG_TIMEOUT_SECONDS * WATCHDOG_CLOCK_HZ)



/* ============================================================
 * GPIO
 * ============================================================ */

/** 拍照补光灯引脚，高电平点亮；SN配置窗口内复用为状态指示。 */
#define IMAGE_TX_LED_PIN         8u
/** 上电后允许执行0x40～0x46出厂SN配置的时间。 */
#define SN_CONFIG_WINDOW_MS      3000u
/** SN配置窗口期间LED翻转周期；100 ms为快速闪烁。 */
#define SN_CONFIG_LED_TOGGLE_MS  100u



#endif /* TX_CONFIG_H */
