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


#if UART_BRIDGE_STARTUP_TEST
/** 上电后发给STM32的一次性UART连通性测试字符串。 */
static const uint8_t m_uart_startup_test[] =
    "\r\nNRF52832 UART READY 1000000 8N1\r\n";
#endif


/* ============================================================
 * 应用初始化
 * ============================================================ */

/** @brief 初始化RTT日志前端和默认后端，并输出当前固件角色。 */
static void receiver_log_init(void)
{
    uint32_t error;

    error = NRF_LOG_INIT(NULL);
    APP_ERROR_CHECK(error);
    NRF_LOG_DEFAULT_BACKENDS_INIT();
    NRF_LOG_INFO("Role: dedicated Radio-to-UART receiver (LEGACY protocol)");
}

/** @brief 初始化上电即清空的RX绑定状态。 */
static void receiver_binding_state_init(void)
{
    receiver_binding_init();
    NRF_LOG_INFO("Binding state: unbound after power-on/reset (RAM only)");
}

/** @brief 初始化12路天线开关；配置非法时记录日志并停止启动。 */
static void receiver_rf_frontend_init(void)
{
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
}

/** @brief 初始化与STM32连接的UART、包间静默定时器和收发模式控制脚。 */
static void receiver_uart_link_init(void)
{
    receiver_mode_pin_init();
    receiver_uart_idle_timer_init();
    receiver_uart_init();
}

/** @brief 初始化Radio接收和非阻塞12路天线管理器。 */
static void receiver_radio_link_init(void)
{
    receiver_radio_init();
    receiver_antenna_manager_init();
}

/**
 * @brief 按依赖顺序完成RX应用初始化。
 * @note 高频时钟必须先于Radio；RF前端和模式脚必须先于Radio收发。
 */
static void receiver_application_init(void)
{
    receiver_clock_init();                     // 1 时钟初始化
    receiver_log_init();                       // 2 日志初始化
    receiver_binding_state_init();             // 3 绑定初始化
    receiver_rf_frontend_init();               // 4 12路天线开关初始化
    receiver_uart_link_init();                 // 5 uart初始化
    receiver_radio_link_init();
    NRF_LOG_INFO("[BOOT] RX firmware=%u.%u.%u device_id:",
                 (unsigned)VERSION_MAIN, (unsigned)VERSION_SUB,
                 (unsigned)VERSION_TEST);
    NRF_LOG_HEXDUMP_INFO((const uint8_t *)NRF_FICR->DEVICEID, 8u);
    NRF_LOG_INFO("[BOOT] radio=%uMHz mode=2M packet=%u ACK_power=-8dBm",
                 (unsigned)RADIO_FREQUENCY_MHZ,
                 (unsigned)RADIO_PACKET_SIZE);
    NRF_LOG_INFO("[BOOT] UART->STM=1Mbps TX=P0.%02u RX=P0.%02u DMA_chunk=%u",
                 (unsigned)UART_TX_PIN, (unsigned)UART_RX_PIN,
                 (unsigned)UART_TX_DMA_CHUNK_SIZE);
    NRF_LOG_INFO("[BOOT] antenna_count=%u default=ANT%u discovery_dwell=%ums",
                 (unsigned)RF1662_ANTENNA_COUNT,
                 (unsigned)(RF1662_DEFAULT_ANTENNA + 1u),
                 (unsigned)RF1662_DISCOVERY_DWELL_MS);
    NRF_LOG_FLUSH();
}


/* ============================================================
 * 主循环服务
 * ============================================================ */

/**
 * @brief 执行一轮非阻塞通信服务。
 * @return true表示本轮处理了UART、Radio、控制事务或UART发送工作。
 */
static bool receiver_application_service(void)
{
    bool did_work = false;

    /* 扫描中捕获 END 后需赶在 TX 的 30ms 应答窗口内发请求。 */
    did_work |= receiver_antenna_service();
    did_work |= receiver_forward_one();
    did_work |= receiver_uart_process_received();
    did_work |= receiver_control_service();
    did_work |= receiver_uart_tx_service();
    did_work |= receiver_antenna_service();
    return did_work;
}


/**
 * @brief NRF_RX程序入口：初始化绑定、12路天线、UART和Radio并运行桥接循环。
 * @return 嵌入式主循环不会退出，返回值仅满足C语言入口约定。
 */
int main(void)
{
    receiver_application_init();

    while (true)
    {
        bool did_work = receiver_application_service();
        bool log_work = NRF_LOG_PROCESS();

        if (!did_work && !log_work)
        {
            __WFE();
        }
    }
}
