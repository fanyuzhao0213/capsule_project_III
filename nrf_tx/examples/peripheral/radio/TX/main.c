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

    /* 若本次上电是看门狗复位导致，输出警告并清除已读取的 RESETREAS。 */
    if ((reset_reason & POWER_RESETREAS_DOG_Msk) != 0u)
    {
        NRF_LOG_WARNING("Previous reset was caused by watchdog timeout");
    }
    NRF_POWER->RESETREAS = reset_reason;

    /* CPU 进入 WFE 时看门狗继续运行；调试器暂停 CPU 时看门狗暂停。 */
    NRF_WDT->CONFIG =
        (WDT_CONFIG_SLEEP_Run << WDT_CONFIG_SLEEP_Pos) |
        (WDT_CONFIG_HALT_Pause << WDT_CONFIG_HALT_Pos);
    NRF_WDT->CRV = WATCHDOG_RELOAD_TICKS;
    NRF_WDT->RREN = WDT_RREN_RR0_Msk;
    watchdog_feed();
    NRF_WDT->TASKS_START = 1u;

    NRF_LOG_INFO("Watchdog ready: timeout=%u s, run in sleep, pause on debug halt",
                 (unsigned)WATCHDOG_TIMEOUT_SECONDS);
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
    NRF_LOG_INFO("TIMER1 ready: 1 ms tick, image period=%u ms",
                 (unsigned)IMAGE_PERIOD_MS);
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

    /* 启动 16 MHz 高频晶振供 Radio 和 CX93510 使用。 */
    image_clock_init();

    /* RTT 日志初始化。 */
    error = NRF_LOG_INIT(NULL);
    APP_ERROR_CHECK(error);
    NRF_LOG_DEFAULT_BACKENDS_INIT();

	#if 1  /* ==== 测试用：写入固定 SN 到 Flash ==== */
    /* 测试步骤：
     *   1. 编译运行此版本，RTT 应打印 "Capsule SN written to Flash"
     *   2. 烧录后重启（重新上电或按 Reset），应看到 "using Flash-bound SN"
     *   3. 测试完成后把这段 #if 1 改成 #if 0 即可关闭
     */
    {
        capsule_sn_t test_sn = {{
            0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88
        }};
        NRF_LOG_INFO("=== TEST: Writing fixed SN 0x11..0x88 to Flash ===");
        capsule_sn_storage_write(&test_sn);
    }
	#endif
	
    /* 从 FICR DEVICEID 读取 8 字节胶囊序列号。 */
    capsule_sn_storage_init();
    capsule_sn_refresh();

    NRF_LOG_INFO("Image radio starting (RTT)");
    NRF_LOG_INFO("Capsule SN (FICR DEVICEID):");
    NRF_LOG_HEXDUMP_INFO(g_capsule_sn, sizeof(g_capsule_sn));
    NRF_LOG_INFO("Image schedule: period=%u ms, repeat=%s, block passes=%u",
                 (unsigned)IMAGE_PERIOD_MS,
                 IMAGE_REPEAT_SEND_ENABLED ? "enabled" : "disabled",
                 (unsigned)IMAGE_BLOCK_PASSES);

    /* 启动看门狗。 */
    watchdog_init();

    /* 初始化图像发送指示灯。 */
    image_tx_led_init();

    NRF_LOG_FLUSH();

    /* 初始化 CX93510 JPEG 控制器。 */
    if (!cx93510_init(&cx_revision))
    {
        image_tx_led_set(false);
        NRF_LOG_ERROR("CX93510 SPI detection/init failed");
        NRF_LOG_FLUSH();
        APP_ERROR_HANDLER(NRF_ERROR_NOT_FOUND);
    }
    NRF_LOG_INFO("CX93510 detected, revision=%u", (unsigned)cx_revision);

    /* 初始化 OV7676 摄像头。 */
    if (!ov7676_init(&sensor_id, &sensor_revision))
    {
        image_tx_led_set(false);
        NRF_LOG_ERROR("OV7676 init/ID check failed");
        NRF_LOG_FLUSH();
        APP_ERROR_HANDLER(NRF_ERROR_NOT_FOUND);
    }
    NRF_LOG_INFO("OV7676 detected, id=0x%04x revision=0x%02x",
                 sensor_id, sensor_revision);

    /* 配置 Radio 收发链路。 */
    radio_configure_image_link();

    /* 启动 1 ms 周期定时器。 */
    timer_init();

    /* 首帧立即采集。 */
    g_capture_due = true;

    /* 主循环：处理接收包 -> ACK 检测 -> 采集请求 -> 分片发送 -> RTT -> 喂狗 -> WFE。 */
    while (true)
    {
        radio_rx_process();
        image_ack_service();
        image_capture_task();
        image_tx_service();
        (void)NRF_LOG_PROCESS();
        watchdog_feed();
        __WFE();
    }
}

