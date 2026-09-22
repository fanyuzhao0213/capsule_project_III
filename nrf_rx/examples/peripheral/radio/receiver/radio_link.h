/**
 * @file radio_link.h
 * @brief nRF Radio收发和接收队列。
 */

#ifndef RX_RADIO_LINK_H
#define RX_RADIO_LINK_H

#include <stdbool.h>
#include <stdint.h>

/** @brief 初始化2400 MHz Radio并开始连续接收。 */
void receiver_radio_init(void);

/** @brief 处理接收队列中的一个无线包。 */
bool receiver_radio_process_one(void);

/** @brief 清空切换天线前残留的无线包。 */
void receiver_radio_clear_queue(void);

/** @brief 安全切换天线并恢复Radio接收。 */
void receiver_radio_select_antenna(uint8_t antenna);

/** @brief 暂停接收，重复发送固定Radio包，然后恢复接收。 */
bool receiver_radio_send(const uint8_t *packet, uint16_t length,
                         uint8_t repeat_count);

/** @brief Radio END中断。 */
void RADIO_IRQHandler(void);

#endif /* RX_RADIO_LINK_H */
