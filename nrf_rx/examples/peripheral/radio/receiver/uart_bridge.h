/**
 * @file uart_bridge.h
 * @brief 专用接收板 UART 桥接：接收 STM32 数据 + 发送图像到 STM32
 *
 * 本文件包含 main.c 中 UART 相关结构、函数与中断处理器的声明，
 * 配合 uart_bridge.c 使用。
 */

#ifndef RX_UART_BRIDGE_H
#define RX_UART_BRIDGE_H

#include <stdbool.h>
#include <stdint.h>
#include "app_uart.h"



/** @brief UART RX 字节总数。 */
extern volatile uint32_t g_uart_rx_total;

/** @brief UART RX 队列满丢包计数。 */
extern volatile uint32_t g_uart_rx_dropped;

/** @brief UART 通信/FIFO 错误计数。 */
extern volatile uint32_t g_uart_rx_errors;

/** @brief 主循环中当前正在拼装的 STM 数据包是否处于活动状态。 */
extern volatile bool g_uart_rx_packet_active;

/** @brief 主循环中 500 ms UART 静默定时器超时标志。 */
extern volatile bool g_uart_rx_idle_timeout;



/** @brief 启动 16 MHz 高频晶振。 */
void receiver_clock_init(void);

/** @brief 初始化 TIMER2 静默定时器（500 ms 无新字节认为一包结束）。 */
void receiver_uart_idle_timer_init(void);

/** @brief 初始化 APP_UART 1 Mbps 收发。 */
void receiver_uart_init(void);

/** @brief 主循环收集 UART 字节并在静默超时后通过 RTT 打印整包。 */
bool receiver_uart_process_received(void);

/** @brief 分批把待发送 STM 帧填入 SDK UART FIFO。 */
bool receiver_uart_tx_service(void);

/** @brief 将一段 UART 二进制数据写入 SDK TX FIFO。 */
bool receiver_uart_write(const uint8_t *data, uint32_t length);

/** @brief 初始化 P0.23 外部模式控制脚。 */
void receiver_mode_pin_init(void);

/** @brief TIMER2 中断：通知主循环 500 ms 已到。 */
void TIMER2_IRQHandler(void);



#endif /* RX_UART_BRIDGE_H */

