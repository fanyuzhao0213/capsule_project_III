/**
 * @file main.c
 * @brief nRF52832 摄像头图像无线发送程序入口
 *
 * 本文件仅负责启动顺序与主循环，摄像头、Radio、图像发送状态机
 * 等业务代码已剥离到 image.c 中。
 */

/* ========================================================================== */
/* 摄像头板：OV7676 -> CX93510 JPEG -> Radio分片发送，同时保持中断接收       */
/* ========================================================================== */

#include <stdbool.h>
#include <stdint.h>
#include "app_error.h"
#include "capsule_sn_storage.h"
#include "config.h"
#include "cx93510.h"
#include "image.h"
#include "legacy_protocol.h"
#include "nrf.h"
#include "nrf_log.h"
#include "nrf_log_ctrl.h"
#include "nrf_log_default_backends.h"
#include "ov7676.h"

#if !TX_CONFIG_LOG_ENABLED
#undef NRF_LOG_INFO
#undef NRF_LOG_WARNING
#undef NRF_LOG_ERROR
#undef NRF_LOG_HEXDUMP_INFO
#define NRF_LOG_INFO(...)
#define NRF_LOG_WARNING(...)
#define NRF_LOG_ERROR(...)
#define NRF_LOG_HEXDUMP_INFO(...)
#endif



/*
********************************************************************************
* Function Name : watchdog_feed
* Description   : 喂硬件看门狗，重新开始本轮超时倒计时。
* Parameter     : None
* Return        : None
********************************************************************************
*/
static void watchdog_feed(void)
{
    NRF_WDT->RR[0] = WDT_RR_RR_Reload;
}



/*
********************************************************************************
* Function Name : watchdog_init
* Description   : 初始化并启动 nRF52832 硬件看门狗。
* Parameter     : None
* Return        : None
********************************************************************************
*/
static void watchdog_init(void)
{
    uint32_t reset_reason = NRF_POWER->RESETREAS;

    /* 清除已读取的复位原因；常规运行阶段不输出日志。 */
    NRF_POWER->RESETREAS = reset_reason;

    /* CPU 进入 WFE 时看门狗继续运行；调试器暂停 CPU 时看门狗暂停。 */
    NRF_WDT->CONFIG =
        (WDT_CONFIG_SLEEP_Run << WDT_CONFIG_SLEEP_Pos) |
        (WDT_CONFIG_HALT_Pause << WDT_CONFIG_HALT_Pos);
    NRF_WDT->CRV = WATCHDOG_RELOAD_TICKS;
    NRF_WDT->RREN = WDT_RREN_RR0_Msk;
    watchdog_feed();
    NRF_WDT->TASKS_START = 1u;

}



/*
********************************************************************************
* Function Name : timer_init
* Description   : 初始化 TIMER1 产生 1 ms 周期中断。
* Parameter     : None
* Return        : None
********************************************************************************
*/
static void timer_init(void)
{
    NRF_TIMER1->TASKS_STOP  = 1u;
    NRF_TIMER1->TASKS_CLEAR = 1u;
    NRF_TIMER1->MODE        = TIMER_MODE_MODE_Timer;
    NRF_TIMER1->BITMODE     = TIMER_BITMODE_BITMODE_32Bit;
    NRF_TIMER1->PRESCALER   = 4u;     /* 16 MHz / 2^4 = 1 MHz */
    NRF_TIMER1->CC[0]       = 1000u;  /* 1 MHz / 1000 = 1 ms */
    NRF_TIMER1->SHORTS      = TIMER_SHORTS_COMPARE0_CLEAR_Msk;
    NRF_TIMER1->INTENSET    = TIMER_INTENSET_COMPARE0_Msk;
    NVIC_ClearPendingIRQ(TIMER1_IRQn);
    NVIC_SetPriority(TIMER1_IRQn, 7u);
    NVIC_EnableIRQ(TIMER1_IRQn);
    NRF_TIMER1->TASKS_START = 1u;
}



/*
********************************************************************************
* Function Name : main
* Description   : 摄像头发送板程序入口。
* Parameter     : None
* Return        : int 满足 ANSI/ISO 标准的返回值
********************************************************************************
*/
int main(void)
{
    uint32_t error;
    uint8_t  cx_revision;
    uint8_t  sensor_revision;
    uint16_t sensor_id;
    uint32_t config_start_ms;
    uint32_t led_toggle_ms;
    bool config_led_on = false;

    /* 启动 16 MHz 高频晶振供 Radio 和 CX93510 使用。 */
    image_clock_init();

    /* RTT 日志初始化。 */
    error = NRF_LOG_INIT(NULL);
    APP_ERROR_CHECK(error);
    NRF_LOG_DEFAULT_BACKENDS_INIT();

    /* 从 FICR DEVICEID 读取 8 字节胶囊序列号。 */
    capsule_sn_storage_init();
    capsule_sn_refresh();


    /* 启动看门狗。 */
    watchdog_init();

    /* 初始化图像发送指示灯。 */
    image_tx_led_init();

    NRF_LOG_FLUSH();

    /* 保持旧工程已验证的启动顺序：先完成图像器件初始化，再开放
     * Radio配置窗口；3秒从LED开始快闪时才正式计时。 */
    if (!cx93510_init(&cx_revision))
    {
        image_tx_led_set(false);
        NRF_LOG_FLUSH();
        APP_ERROR_HANDLER(NRF_ERROR_NOT_FOUND);
    }

    if (!ov7676_init(&sensor_id, &sensor_revision))
    {
        image_tx_led_set(false);
        NRF_LOG_FLUSH();
        APP_ERROR_HANDLER(NRF_ERROR_NOT_FOUND);
    }

    radio_configure_image_link();
    timer_init();

    capsule_sn_config_window_begin();
    config_start_ms = g_time_ms;
    led_toggle_ms = g_time_ms;
    NRF_LOG_INFO("[SN CONFIG] boot window=%u ms, LED toggle=%u ms",
                 (unsigned)SN_CONFIG_WINDOW_MS,
                 (unsigned)SN_CONFIG_LED_TOGGLE_MS);
    NRF_LOG_HEXDUMP_INFO((const uint8_t *)NRF_FICR->DEVICEID, 8u);

    while (!capsule_sn_config_is_complete())
    {
        /* Always process a packet before checking timeout. A valid command
         * received in the last millisecond therefore resets the full 3 s
         * inactivity window instead of being discarded at the boundary. */
        radio_rx_process();
        if (capsule_sn_config_is_complete())
        {
            break;
        }
        if ((uint32_t)(g_time_ms -
                       capsule_sn_config_last_activity_ms()) >=
            SN_CONFIG_WINDOW_MS)
        {
            break;
        }
        if ((uint32_t)(g_time_ms - led_toggle_ms) >=
            SN_CONFIG_LED_TOGGLE_MS)
        {
            led_toggle_ms = g_time_ms;
            config_led_on = !config_led_on;
            image_tx_led_set(config_led_on);
        }
        (void)NRF_LOG_PROCESS();
        watchdog_feed();
        __WFE();
    }

    capsule_sn_config_window_end();
    image_tx_led_set(false);
    if (capsule_sn_config_is_complete())
    {
        NRF_LOG_INFO("[SN CONFIG] SUCCESS at %u ms; entering normal startup",
                     (unsigned)(g_time_ms - config_start_ms));
    }
    else
    {
        NRF_LOG_INFO("[SN CONFIG] TIMEOUT after %u ms; entering normal startup",
                     (unsigned)(g_time_ms - config_start_ms));
    }
    NRF_LOG_FLUSH();

    if (capsule_sn_config_is_complete())
    {
        capsule_sn_broadcast_burst(SN_BROADCAST_STARTUP_REPEAT);
    }

#if IMAGE_TRANSMISSION_ENABLED
    /* 正式图片模式下首帧立即采集。 */
    g_capture_due = true;
#endif

    /* 主循环：处理接收包 -> ACK 检测 -> 采集请求 -> 分片发送 -> RTT -> 喂狗 -> WFE。 */
    while (true)
    {
        radio_rx_process();
        image_ack_service();
        capsule_sn_broadcast_service();
#if IMAGE_TRANSMISSION_ENABLED
        image_capture_task();
        image_tx_service();
#endif
        (void)NRF_LOG_PROCESS();
        watchdog_feed();
        __WFE();
    }
}
