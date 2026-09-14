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
#include "config.h"
#include "cx93510.h"
#include "dev_adxl362.h"
#include "image.h"
#include "nrf.h"
#include "nrf_log.h"
#include "nrf_log_ctrl.h"
#include "nrf_log_default_backends.h"
#include "ov7676.h"

#if !(TX_LOG_ENABLED && (TX_BOOT_LOG_ENABLED || TX_CONFIG_LOG_ENABLED))
#undef NRF_LOG_INFO
#undef NRF_LOG_WARNING
#undef NRF_LOG_ERROR
#undef NRF_LOG_HEXDUMP_INFO
#define NRF_LOG_INFO(...)
#define NRF_LOG_WARNING(...)
#define NRF_LOG_ERROR(...)
#define NRF_LOG_HEXDUMP_INFO(...)
#define TX_CONFIG_LOG_FLUSH() ((void)0)
#else
#define TX_CONFIG_LOG_FLUSH() NRF_LOG_FLUSH()
#endif



/*
********************************************************************************
* Function Name : watchdog_feed
* Description   : 喂硬件看门狗，重新开始本轮超时倒计时。
* Parameter     : None
* Return        : None
********************************************************************************
*/
/** @brief 主循环和配置等待期间定期喂硬件看门狗。 */
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
/** @brief 初始化3秒硬件看门狗；休眠时运行，调试暂停时停止计时。 */
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
/** @brief 初始化1ms系统节拍，为配置超时、广播周期和图片周期提供时间基准。 */
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

/** @brief 开启拍照/发送活动阶段所需的HFXO和1ms TIMER1。 */
static void active_clock_start(void)
{
    /* HFCLKSTAT.STATE=Running 对内部HFINT和外部HFXO都成立，不能据此
     * 判断Radio所需的晶振已经恢复。低功耗释放HFXO后必须检查SRC。 */
    if ((NRF_CLOCK->HFCLKSTAT & CLOCK_HFCLKSTAT_SRC_Msk) !=
        (CLOCK_HFCLKSTAT_SRC_Xtal << CLOCK_HFCLKSTAT_SRC_Pos))
    {
        NRF_CLOCK->EVENTS_HFCLKSTARTED = 0u;
        NRF_CLOCK->TASKS_HFCLKSTART = 1u;
        while (NRF_CLOCK->EVENTS_HFCLKSTARTED == 0u) {}
    }
    NRF_TIMER1->TASKS_CLEAR = 1u;
    NRF_TIMER1->EVENTS_COMPARE[0] = 0u;
    NRF_TIMER1->TASKS_START = 1u;
}

/** @brief 活动阶段结束后关闭1ms TIMER1并释放外部高频晶振。 */
static void active_clock_stop(void)
{
    NRF_TIMER1->TASKS_STOP = 1u;
    NRF_TIMER1->TASKS_CLEAR = 1u;
    NRF_TIMER1->EVENTS_COMPARE[0] = 0u;
    NRF_CLOCK->TASKS_HFCLKSTOP = 1u;
}



/* ============================================================
 * 应用初始化与运行阶段
 * ============================================================ */

#if TX_LOG_ENABLED
/** @brief 初始化Nordic日志前端和RTT后端；总开关关闭时本函数不参与编译。 */
static void tx_log_init(void)
{
    uint32_t error = NRF_LOG_INIT(NULL);
    APP_ERROR_CHECK(error);
    NRF_LOG_DEFAULT_BACKENDS_INIT();
}

#define TX_LOG_INIT()       tx_log_init()
#define TX_LOG_PROCESS()    ((void)NRF_LOG_PROCESS())
#else
#define TX_LOG_INIT()       ((void)0)
#define TX_LOG_PROCESS()    ((void)0)
#endif

/** @brief 初始化CX93510和OV7676，校验成功后让传感器进入软件休眠。 */
static void camera_init(void)
{
    uint8_t cx_revision;
    uint8_t sensor_revision;
    uint16_t sensor_id;

    if (!cx93510_init(&cx_revision))                                           // ① 初始化JPEG压缩器和帧缓冲
    {
        image_tx_led_set(false);
        TX_CONFIG_LOG_FLUSH();
        APP_ERROR_HANDLER(NRF_ERROR_NOT_FOUND);
    }
    if (!ov7676_init(&sensor_id, &sensor_revision))                            // ② 初始化并校验摄像头寄存器
    {
        image_tx_led_set(false);
        TX_CONFIG_LOG_FLUSH();
        APP_ERROR_HANDLER(NRF_ERROR_NOT_FOUND);
    }
    if (!ov7676_sleep())                                                        // ③ 非拍摄阶段关闭视频流以降低功耗
    {
        image_tx_led_set(false);
        TX_CONFIG_LOG_FLUSH();
        APP_ERROR_HANDLER(NRF_ERROR_INTERNAL);
    }
}

/** @brief 按依赖顺序完成TX板全部一次性初始化。 */
static void application_init(void)
{
    image_clock_init();                                                         // ① Radio和CX93510使用的高频晶振
    TX_LOG_INIT();                                                              // ② 日志总开关关闭时编译为空操作
    capsule_sn_init();                                                          // ③ 加载Flash SN或FICR DEVICEID
    watchdog_init();                                                            // ④ 启动异常恢复看门狗
    image_tx_led_init();                                                        // ⑤ 初始化补光灯/配置状态灯
    camera_init();                                                              // ⑥ 初始化图像链路并休眠OV7676
    if (!adxl362_init())                                                        // ⑦ INT保持高阻，仅轮询读取
    {
        NRF_LOG_ERROR("[ADXL362] initialization failed");
    }
    else
    {
        NRF_LOG_INFO("[BOOT] ADXL362 ready: SPI polling, INT unused");
    }
    radio_configure_image_link();                                               // ⑧ 初始化2.4GHz收发链路
    timer_init();                                                               // ⑨ 最后启动1ms业务时基

#if TX_LOG_ENABLED && TX_BOOT_LOG_ENABLED
    NRF_LOG_INFO("[BOOT] TX firmware=%u.%u.%u device_id:",
                 (unsigned)VERSION_MAIN, (unsigned)VERSION_SUB,
                 (unsigned)VERSION_TEST);
    NRF_LOG_HEXDUMP_INFO((const uint8_t *)NRF_FICR->DEVICEID, 8u);
    NRF_LOG_INFO("[BOOT] active capsule SN:");
    NRF_LOG_HEXDUMP_INFO(g_capsule_sn, LEGACY_CAPSULE_SN_SIZE);
    NRF_LOG_INFO("[BOOT] radio=%uMHz mode=2M packet=%u TX image=%u",
                 (unsigned)RADIO_FREQUENCY_MHZ,
                 (unsigned)RADIO_PACKET_SIZE,
                 (unsigned)IMAGE_TRANSMISSION_ENABLED);
    NRF_LOG_INFO("[BOOT] image_period=%ums SN_period=%ums gap=%ums retries=%u",
                 (unsigned)IMAGE_PERIOD_MS,
                 (unsigned)SN_BROADCAST_PERIOD_MS,
                 (unsigned)IMAGE_FRAGMENT_GAP_MS,
                 (unsigned)IMAGE_MAX_RETRIES);
#endif
}

/** @brief 执行上电3秒SN配置窗口，最后关闭窗口和状态灯。 */
static void sn_config_window_run(void)
{
    uint32_t led_toggle_ms = g_time_ms;
    bool config_led_on = false;
#if TX_LOG_ENABLED && (TX_BOOT_LOG_ENABLED || TX_CONFIG_LOG_ENABLED)
    uint32_t config_start_ms = g_time_ms;
#endif

    capsule_sn_config_window_begin();                                           // ① 开放0x40～0x46出厂配置命令
    NRF_LOG_INFO("[SN CONFIG] boot window=%u ms, LED toggle=%u ms",
                 (unsigned)SN_CONFIG_WINDOW_MS,
                 (unsigned)SN_CONFIG_LED_TOGGLE_MS);
    NRF_LOG_HEXDUMP_INFO((const uint8_t *)NRF_FICR->DEVICEID, 8u);

    while (!capsule_sn_config_is_complete())
    {
        radio_rx_process();                                                     // ② 先处理包，保证最后1ms收到的命令仍可续期
        if (capsule_sn_config_is_complete())
        {
            break;
        }
        if ((uint32_t)(g_time_ms -
                       capsule_sn_config_last_activity_ms()) >=
            SN_CONFIG_WINDOW_MS)                                                // ③ 连续3秒无合法配置则退出
        {
            break;
        }
        if ((uint32_t)(g_time_ms - led_toggle_ms) >=
            SN_CONFIG_LED_TOGGLE_MS)                                            // ④ 配置期间100ms翻转状态灯
        {
            led_toggle_ms = g_time_ms;
            config_led_on = !config_led_on;
            image_tx_led_set(config_led_on);
        }
        TX_LOG_PROCESS();                                                       // ⑤ 总开关关闭时不执行日志后台处理
        watchdog_feed();
        __WFE();
    }

    capsule_sn_config_window_end();                                             // ⑥ 后续拒绝出厂SN配置指令
    image_tx_led_set(false);
#if TX_LOG_ENABLED && (TX_BOOT_LOG_ENABLED || TX_CONFIG_LOG_ENABLED)
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
#endif
    TX_CONFIG_LOG_FLUSH();
}

/** @brief 正常运行阶段：处理控制、广播、采集、发送和低功耗等待。 */
static void application_run(void)
{
    bool active_clock_running = true;
    while (true)
    {
        if (image_runtime_busy() && !active_clock_running)
        {
            active_clock_start();
            active_clock_running = true;
        }
        radio_rx_process();                                                     // ① 处理控制应答或图像ACK
        image_ack_service();                                                    // ② ACK超时后按协议重发
        capsule_sn_broadcast_service();                                         // ③ 按配置周期广播当前SN
#if IMAGE_TRANSMISSION_ENABLED
        image_capture_task();                                                   // ④ 到期后唤醒摄像头并采集
        image_tx_service();                                                     // ⑤ 每轮最多发送一个图片分片
#endif
        TX_LOG_PROCESS();                                                       // ⑥ 日志关闭时不执行任何后台处理
        watchdog_feed();                                                        // ⑦ 证明主循环仍正常运行
        if (active_clock_running && !image_runtime_busy())
        {
            active_clock_stop();
            active_clock_running = false;
        }
        __WFE();                                                                // ⑧ 无事件时CPU睡眠，等待中断唤醒
    }
}



/*
********************************************************************************
* Function Name : main
* Description   : 摄像头发送板程序入口。
* Parameter     : None
* Return        : int 满足 ANSI/ISO 标准的返回值
********************************************************************************
*/
/**
 * @brief NRF_TX程序入口：初始化硬件、执行SN配置窗口并进入广播/图片循环。
 * @return 嵌入式主循环不会退出，返回值仅满足C语言入口约定。
 */
int main(void)
{
    application_init();                                                        // ① 完成一次性硬件与业务初始化
    sn_config_window_run();                                                    // ② 执行上电出厂SN配置窗口
    radio_enter_idle();                                                        // ③ 配置结束后关闭持续RX，后续仅ACK窗口按需开启

    /* 无论本次配置成功还是超时，都广播当前生效SN三次：
     * 成功时为刚写入Flash的新SN，未成功时为Flash中原有SN。 */
    capsule_sn_broadcast_burst(SN_BROADCAST_STARTUP_REPEAT);                   // ④ 启动广播
    image_low_power_scheduler_init();                                          // ⑤ RTC2负责低功耗周期唤醒

#if IMAGE_TRANSMISSION_ENABLED
    g_capture_due = true;                                                       // ⑥ 正式图片模式首帧立即采集
#endif
    application_run();                                                         // ⑦ 进入正常业务与低功耗等待循环
}
