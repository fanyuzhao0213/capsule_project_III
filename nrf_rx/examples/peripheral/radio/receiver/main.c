/**
 * @file main.c
 * @brief nRF52832 专用接收板程序入口（仅 LEGACY 协议）
 *
 * 本文件仅负责启动顺序与主循环，图像接收、协议解析、UART 桥接
 * 等业务代码已剥离到 image.c 和 uart_bridge.c 中。
 *
 * 数据链路：摄像头发送板 -> Radio -> 环形队列 -> LEGACY 协议解析
 *         -> 1 Mbps UART -> STM32 主控 -> PC。
 */

/* ========================================================================== */
/* 专用接收板：Radio -> 环形队列 -> 1 Mbps UART -> STM32 主控                */
/* ========================================================================== */

#include <stdbool.h>
#include <stdint.h>
#include "app_error.h"
#include "binding_storage.h"
#include "config.h"
#include "image.h"
#include "legacy_protocol.h"
#include "nrf.h"
#include "nrf_log.h"
#include "nrf_log_ctrl.h"
#include "nrf_log_default_backends.h"
#include "rf1662.h"
#include "uart_bridge.h"



/*
********************************************************************************
* Function Name : main
* Description   : 专用接收板程序入口（仅 LEGACY 协议）。
* Parameter     : None
* Return        : int 满足 ANSI/ISO 标准的返回值
*
* 启动顺序：高频时钟 -> RTT -> P0.23 -> 500 ms 静默定时器 -> 1 Mbps UART
*          -> Radio 中断接收 -> 首帧可选 UART 测试串。
* 主循环：UART 接收 -> Radio LEGACY 协议解析 -> UART 发送 -> RTT 日志 -> 空闲时 WFE。
********************************************************************************
*/
int main(void)
{
    uint32_t error;
#if UART_BRIDGE_STARTUP_TEST
    static const uint8_t startup_test[] =
        "\r\nNRF52832 UART READY 1000000 8N1\r\n";
#endif

    /* 启动 16 MHz 高频晶振供 Radio 使用。 */
    receiver_clock_init();

    /* RTT 日志初始化。 */
    error = NRF_LOG_INIT(NULL);
    APP_ERROR_CHECK(error);
    NRF_LOG_DEFAULT_BACKENDS_INIT();
    NRF_LOG_INFO("Role: dedicated Radio-to-UART receiver (LEGACY protocol)");

    receiver_binding_init();
    NRF_LOG_INFO("Binding state: unbound after power-on/reset (RAM only)");

    /* 初始化 12 路射频开关并固定选择一路；必须在 Radio 启动前完成。 */
    if (!rf1662_init(RF1662_DEFAULT_ANTENNA))
    {
        NRF_LOG_ERROR("RF1662 invalid antenna index: %u",
                      (unsigned)RF1662_DEFAULT_ANTENNA);
        NRF_LOG_FLUSH();
        APP_ERROR_HANDLER(NRF_ERROR_INVALID_PARAM);
    }
    NRF_LOG_INFO("RF1662 ready: SCLK=P0.%02u SDATA=P0.%02u antenna=ANT%u",
                 (unsigned)RF1662_SCLK_PIN,
                 (unsigned)RF1662_SDATA_PIN,
                 (unsigned)(rf1662_get_antenna() + 1u));

    /* 初始化 P0.23 模式控制脚，接收期间输出低电平。 */
    receiver_mode_pin_init();

    /* 初始化 500 ms UART 静默定时器和 1 Mbps UART。 */
    receiver_uart_idle_timer_init();
    receiver_uart_init();

#if UART_BRIDGE_STARTUP_TEST
    /* 启动后发送一次 ASCII 测试串给 STM/PC，便于验证 UART 链路。 */
    NRF_LOG_INFO("Sending one-time UART startup test to STM");
    NRF_LOG_FLUSH();
    if (!receiver_uart_write(startup_test, sizeof(startup_test) - 1u))
    {
        NRF_LOG_ERROR("UART startup test failed");
    }
#endif

    /* 初始化 Radio 中断接收（LEGACY 协议参数）。 */
    receiver_radio_init();
#if RF1662_STARTUP_SCAN_ENABLED
    (void)receiver_scan_best_antenna();
#endif
    NRF_LOG_FLUSH();

    /* 主循环：UART 接收 -> Radio LEGACY 协议解析 -> UART 发送 -> RTT -> 空闲 WFE。 */
    while (true)
    {
        bool did_work = false;
        bool log_work;

        if (receiver_uart_process_received())
        {
            did_work = true;
        }
        if (receiver_forward_one())
        {
            did_work = true;
        }
        if (receiver_control_service())
        {
            did_work = true;
        }
        if (receiver_uart_tx_service())
        {
            did_work = true;
        }

        /* 每轮处理一条待输出 RTT 日志，保证连续通信时十六进制日志也能显示。 */
        log_work = NRF_LOG_PROCESS();
        if (!did_work && !log_work)
        {
            __WFE();
        }
    }
}
