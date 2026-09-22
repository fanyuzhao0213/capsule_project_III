/**
 * @file receiver_timebase.c
 * @brief TIMER1统一驱动系统毫秒时间、天线调度和UART静默检测。
 */

#include "receiver_timebase.h"
#include "antenna_manager.h"
#include "config.h"
#include "nrf.h"
#include "uart_bridge.h"

typedef struct
{
    volatile uint32_t now_ms;      /** RX统一毫秒时间。 */
    uint32_t last_tick_us;         /** 上次已折算的TIMER1计数。 */
} receiver_timebase_state_t;

static receiver_timebase_state_t m_timebase;

/** @brief 返回RX统一毫秒时间；允许32位自然回绕。 */
uint32_t receiver_timebase_now_ms(void)
{
    return m_timebase.now_ms;
}

/** @brief TIMER1比较中断：补偿延迟后推进所有毫秒级软件任务。 */
void TIMER1_IRQHandler(void)
{
    uint32_t now_us;
    uint32_t elapsed_ms;

    if (NRF_TIMER1->EVENTS_COMPARE[0] == 0u)
    {
        return;
    }

    NRF_TIMER1->EVENTS_COMPARE[0] = 0u;
    NRF_TIMER1->TASKS_CAPTURE[2] = 1u;                  // CC2读取当前微秒计数
    now_us = NRF_TIMER1->CC[2];
    /* 若中断响应晚于1 ms，一次补齐遗漏的毫秒数，避免软件时间变慢。 */
    elapsed_ms = (uint32_t)(now_us - m_timebase.last_tick_us) / 1000u;
    if (elapsed_ms == 0u)
    {
        elapsed_ms = 1u;
    }

    m_timebase.last_tick_us += elapsed_ms * 1000u;
    m_timebase.now_ms += elapsed_ms;

    receiver_antenna_tick_1ms(elapsed_ms);              // 天线4 ms软件节拍
    receiver_uart_idle_tick_1ms(now_us);                 // UART 100 ms静默检测

    NRF_TIMER1->CC[0] = m_timebase.last_tick_us + 1000u; // 安排下一个1 ms边界
}

/** @brief 初始化唯一硬件时间基TIMER1：1 MHz自由运行、每1 ms比较一次。 */
void receiver_timebase_init(void)
{
    NRF_TIMER1->TASKS_STOP = 1u;
    NRF_TIMER1->TASKS_CLEAR = 1u;
    NRF_TIMER1->MODE = TIMER_MODE_MODE_Timer;
    NRF_TIMER1->BITMODE = TIMER_BITMODE_BITMODE_32Bit;
    NRF_TIMER1->PRESCALER = 4u;                         // 16 MHz / 16 = 1 MHz
    NRF_TIMER1->SHORTS = 0u;                            // 保持自由运行
    NRF_TIMER1->EVENTS_COMPARE[0] = 0u;
    NRF_TIMER1->INTENCLR = 0xFFFFFFFFu;
    NRF_TIMER1->CC[0] = 1000u;                          // 1 ms首次比较
    NRF_TIMER1->INTENSET = TIMER_INTENSET_COMPARE0_Msk;

    m_timebase.now_ms = 0u;
    m_timebase.last_tick_us = 0u;

    NVIC_ClearPendingIRQ(TIMER1_IRQn);
    NVIC_SetPriority(TIMER1_IRQn, RX_TIMEBASE_IRQ_PRIORITY);
    NVIC_EnableIRQ(TIMER1_IRQn);
    NRF_TIMER1->TASKS_START = 1u;
}
