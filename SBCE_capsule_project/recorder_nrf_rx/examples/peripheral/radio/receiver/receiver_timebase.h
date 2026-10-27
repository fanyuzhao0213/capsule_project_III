/**
 * @file receiver_timebase.h
 * @brief RX统一TIMER1时间基。
 */

#ifndef RX_RECEIVER_TIMEBASE_H
#define RX_RECEIVER_TIMEBASE_H

#include <stdint.h>

/** @brief 初始化1 MHz自由运行TIMER1和1 ms比较中断。 */
void receiver_timebase_init(void);

/** @brief 返回RX启动后的统一毫秒时间。 */
uint32_t receiver_timebase_now_ms(void);

/** @brief TIMER1比较中断。 */
void TIMER1_IRQHandler(void);

#endif /* RX_RECEIVER_TIMEBASE_H */
