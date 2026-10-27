#include "stm8s.h"
#include "stm8s_it.h"
#include "stm8s_gpio.h"
#include "stm8s_uart1.h"
#include "stm8s_tim4.h"
#include "bsp_uart.h"
#include "bsp_delay.h"

volatile uint8_t sw1_press_flag = 0;
volatile uint8_t sw2_press_flag = 0;


/* UART1接收中断：IDLE表示一段应答结束；主循环取走前不覆盖该段数据。 */
INTERRUPT_HANDLER(UART1_RX_IRQHandler, 18)
{
    uint8_t status;
    uint8_t data;
    /* 先读SR再读DR可清IDLE及错误位；若RXNE同时置位，DR仍是有效字节。 */
    status = UART1->SR;
    if ((status & (UART1_SR_RXNE | UART1_SR_IDLE |
                   UART1_SR_OR | UART1_SR_NF | UART1_SR_FE | UART1_SR_PE)) == 0U)
    {
        return;
    }
    data = UART1->DR;

    if ((status & (UART1_SR_OR | UART1_SR_NF | UART1_SR_FE | UART1_SR_PE)) != 0U)
    {
        rx_packet_error_flag = 1U;
    }
    if ((status & UART1_SR_RXNE) != 0U)
    {
        if (rx_packet_ready_flag == 0U)
        {
            if (rx_packet.len < PACKET_SIZE)
            {
                rx_packet.buffer[rx_packet.len++] = data;
            }
            else
            {
                rx_packet_overflow_flag = 1U;
            }
        }
        else
        {
            rx_packet_overflow_flag = 1U;
        }
    }
    if (((status & UART1_SR_IDLE) != 0U) && (rx_packet.len != 0U))
    {
        rx_packet_ready_flag = 1U;
    }
}

/*
 * TIM4 更新中断：每约1ms进入一次，用于系统毫秒 Tick
 */
INTERRUPT_HANDLER(TIM4_UPD_OVF_IRQHandler, 23)
{
    // 延时系统滴答处理
    Delay_TickISR();
    
    // 清除 TIM4 更新中断标志
    TIM4_ClearITPendingBit(TIM4_IT_UPDATE);
}

/*
 * PORTC 外部中断：PC3 / PC4 共用此入口
 */
INTERRUPT_HANDLER(EXTI_PORTC_IRQHandler, 5)
{
    // PC3 按下（下降沿触发，高变低）
    if (GPIO_ReadInputPin(GPIOC, GPIO_PIN_3) == RESET)
    {
        sw1_press_flag = 1;
    }

    // PC4 按下（下降沿触发，高变低）
    if (GPIO_ReadInputPin(GPIOC, GPIO_PIN_4) == RESET)
    {
        sw2_press_flag = 1;
    }
}

