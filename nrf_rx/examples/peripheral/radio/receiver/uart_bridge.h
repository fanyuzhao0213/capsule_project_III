/**
 * @file uart_bridge.h
 * @brief STM32 UART收包、控制路由和图片DMA发送。
 */

#ifndef RX_UART_BRIDGE_H
#define RX_UART_BRIDGE_H

#include <stdbool.h>
#include <stdint.h>

void receiver_clock_init(void);
void receiver_mode_pin_init(void);
void receiver_uart_idle_capture_init(void);
void receiver_uart_idle_tick_1ms(uint32_t now_us);
void receiver_uart_init(void);

/** @brief 主循环收集并解析STM32发来的完整UART包。 */
bool receiver_uart_rx_service(void);

/** @brief 图片发送空闲后补发排队的短控制应答。 */
bool receiver_uart_control_service(void);

/** @brief 每轮最多启动一个图片DMA块。 */
bool receiver_uart_tx_service(void);

/** @brief 处理Radio控制包；false表示应交给图片模块。 */
bool receiver_control_handle_radio_packet(const uint8_t *packet);

/** @brief 直接写入一段短UART数据。 */
bool receiver_uart_send(const uint8_t *data, uint32_t length);

/** @brief 将设备信息和JPEG封装为0x81帧，交给异步DMA发送。 */
bool receiver_uart_queue_image(const uint8_t *device_info,
                               const uint8_t *jpeg,
                               uint16_t jpeg_length,
                               uint8_t image_id);

/** @brief 查询是否有图片正在等待UART发送完成。 */
bool receiver_uart_image_tx_busy(void);

#endif /* RX_UART_BRIDGE_H */
