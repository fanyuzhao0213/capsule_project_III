/**
 * @file main.c
 * @brief RX启动入口和非阻塞任务调度。
 */

#include "antenna_manager.h"
#include "app_error.h"
#include "binding_storage.h"
#include "config.h"
#include "image.h"
#include "nrf.h"
#include "nrf_log.h"
#include "nrf_log_ctrl.h"
#include "nrf_log_default_backends.h"
#include "radio_link.h"
#include "receiver_timebase.h"
#include "rf1662.h"
#include "uart_bridge.h"

/** @brief 初始化日志前端和已启用的RTT后端。 */
static void receiver_log_init(void)
{
    uint32_t error = NRF_LOG_INIT(NULL);
    APP_ERROR_CHECK(error);
    NRF_LOG_DEFAULT_BACKENDS_INIT();
}

/** @brief 初始化RF1662，并默认接通配置指定的天线。 */
static void receiver_rf_frontend_init(void)
{
    if (!rf1662_init(RF1662_DEFAULT_ANTENNA))
    {
        APP_ERROR_HANDLER(NRF_ERROR_INVALID_PARAM);
    }
}


/* ========== 1. 基础硬件与时钟层（最先执行，无中断） ========== */
static void receiver_platform_init(void)
{
    receiver_clock_init();      // 16 MHz 晶振
    receiver_mode_pin_init();   // 收发模式控制脚
    receiver_rf_frontend_init();// RF1662，默认 ANT1
}

/* ========== 2. 基础服务层（日志、时间基、绑定状态、图像） ========== */
static void receiver_service_init(void)
{
    receiver_log_init();        // RTT 日志
    receiver_binding_init();    // 清空 RAM 绑定状态
    receiver_image_init();      // 图片模块状态
    receiver_timebase_init();   // TIMER1 统一时间基
}

/* ========== 3. 通信外设启动层（会产生中断，必须最后启动） ========== */
static void receiver_comm_init(void)
{
    receiver_uart_idle_capture_init(); // PPI 捕获
    receiver_uart_init();              // 1 Mbps UART
    receiver_radio_init();             // 2 Mbit/s Radio，开始接收
    receiver_antenna_init();           // 天线管理器，进入未绑定发现状态
}



/** @brief 按硬件依赖关系初始化各模块。 */
static void receiver_application_init(void)
{
    /* 先准备基础硬件，再启动可能产生中断的UART、Radio和天线状态机。 */
	receiver_platform_init();   // 硬件
    receiver_service_init();    // 服务
    receiver_comm_init();       // 最后启动中断源

    NRF_LOG_INFO("RX ready: firmware=%u.%u.%u radio=%uMHz",
                 (unsigned)VERSION_MAIN, (unsigned)VERSION_SUB,
                 (unsigned)VERSION_TEST, (unsigned)RADIO_FREQUENCY_MHZ);
    NRF_LOG_INFO("[BOOT] packet=%u bytes UART=1Mbps default=ANT%u",
                 (unsigned)RADIO_PACKET_SIZE,
                 (unsigned)(RF1662_DEFAULT_ANTENNA + 1u));
    NRF_LOG_INFO("[BOOT] seek=%ums x%u fast_scan=%ums x%u",
                 (unsigned)RF1662_SEEK_END_DWELL_MS,
                 (unsigned)RF1662_SEEK_END_ROUNDS,
                 (unsigned)RF1662_FAST_SCAN_DWELL_MS,
                 (unsigned)RF1662_FAST_SCAN_ROUNDS);
    NRF_LOG_INFO("[BOOT] RX device ID:");
    NRF_LOG_HEXDUMP_INFO((const uint8_t *)NRF_FICR->DEVICEID, 8u);
}

/**
 * @brief 执行一轮主循环任务。
 *
 * 顺序保证：先处理Radio紧急事件，再处理业务包和STM控制，最后推进
 * 天线定时状态；每个任务单次工作量有限，不长时间占用主循环。
 */
static bool receiver_application_run_once(void)
{
    bool worked = false;

    worked |= receiver_antenna_service_events();  // 处理END和0x11事件
    worked |= receiver_radio_process_one();       // 解析一个无线业务包
    worked |= receiver_uart_rx_service();         // 解析STM32控制命令
    worked |= receiver_uart_control_service();    // 补发短控制应答
    worked |= receiver_uart_tx_service();         // 启动一个图片DMA块
    worked |= receiver_antenna_service_events();  // 处理本轮新事件
    worked |= receiver_antenna_service_schedule();// 推进扫描或失联切换

    return worked;
}

/** @brief RX程序入口，持续执行非阻塞任务并在空闲时等待中断。 */
int main(void)
{
    receiver_application_init();

    while (true)
    {
        /* 所有业务均为短任务；本轮无任务和日志时才等待下一次中断。 */
        bool worked = receiver_application_run_once();
        bool log_worked = NRF_LOG_PROCESS();

        if (!worked && !log_worked)
        {
            __WFE();
        }
    }
}
