#include "bsp_delay.h"
#include "stm8s_tim4.h"


/*
 * ============================================================================
 * 系统毫秒计数
 *
 * 每经过 1ms：
 *
 *     g_ms_tick++
 *
 * 所以：
 *
 *     g_ms_tick = 1000
 *
 * 大约表示系统运行了 1000ms。
 *
 * volatile 非常重要：
 *
 * 因为这个变量同时被：
 *
 *     主程序
 *     TIM4中断
 *
 * 使用。
 * ============================================================================
 */
static volatile uint32_t g_ms_tick = 0;


/*
 * ============================================================================
 * TIM4 初始化
 *
 * TIM4 是一个 8bit 基本定时器。
 *
 * STM8S103F2P6：
 *
 *     CPU = 16MHz
 *
 * TIM4 时钟通常来自 CPU 时钟。
 *
 * 使用 128 分频：
 *
 *     16MHz / 128
 *     = 125kHz
 *
 * 所以：
 *
 *     1个计数 = 8us
 *
 * 需要 1ms：
 *
 *     1ms / 8us = 125
 *
 * 因此：
 *
 *     ARR = 125 - 1
 *         = 124
 *
 * 所以：
 *
 *     Prescaler = 128
 *     ARR       = 124
 *
 * 得到约 1ms 溢出一次。
 * ============================================================================
 */
void Delay_Init(void)
{
    /*
     * TIM4 复位。
     */
    TIM4_DeInit();


    /*
     * 设置 TIM4：
     *
     * Prescaler：
     *
     *     TIM4_PRESCALER_128
     *
     * Period：
     *
     *     124
     *
     * Counter：
     *
     *     从 0 开始计数。
     */
    TIM4_TimeBaseInit(
        TIM4_PRESCALER_128,
        124
    );


    /*
     * 打开 TIM4 更新中断。
     */
    TIM4_ITConfig(
        TIM4_IT_UPDATE,
        ENABLE
    );


    /*
     * 清除更新标志。
     */
    TIM4_ClearFlag(
        TIM4_FLAG_UPDATE
    );


    /*
     * 启动 TIM4。
     */
    TIM4_Cmd(ENABLE);
}


/*
 * ============================================================================
 * 获取系统 Tick
 * ============================================================================
 */
uint32_t Delay_GetTick(void)
{
    uint32_t tick;


    /*
     * 读取 32bit 变量时，STM8 是 8bit MCU。
     *
     * 为了避免读取过程中刚好被 TIM4 中断修改，
     * 这里暂时关闭全局中断。
     */
    disableInterrupts();

    tick = g_ms_tick;

    enableInterrupts();


    return tick;
}


/*
 * ============================================================================
 * Delay_ms
 *
 * 使用系统 Tick 实现毫秒延时。
 *
 * 例如：
 *
 *     Delay_ms(100);
 *
 * 就等待：
 *
 *     当前Tick + 100
 *
 * ============================================================================
 */
void Delay_ms(uint32_t ms)
{
    uint32_t start;


    start = Delay_GetTick();


    /*
     * 一直等待，直到经过 ms 毫秒。
     *
     * 注意：
     *
     * 这个函数是阻塞式延时。
     *
     * 主循环会停在这里。
     *
     * 但 TIM4 中断仍然会运行。
     */
    while ((Delay_GetTick() - start) < ms)
    {
        /* 等待 */
    }
}
void Delay_TickISR(void)
{
    g_ms_tick++;
}
