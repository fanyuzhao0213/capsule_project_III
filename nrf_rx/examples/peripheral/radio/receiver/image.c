/**
 * @file image.c
 * @brief 专用接收板图像接收、重组与 STM32 转发实现
 *
 * 本文件从原 main.c 中剥离出所有与图像接收相关的代码：Radio 队列、
 * 协议解析、ACK 发送、STM32 帧封装等。
 */

#include "image.h"
#include "binding_storage.h"
#include "config.h"
#include "app_uart.h"
#include "nrf.h"
#include "nrf_delay.h"
#include "nrf_gpio.h"
#include "nrf_log.h"
#include "nrf_log_ctrl.h"
#include "rf1662.h"
#include "uart_bridge.h"
#include <string.h>



/* ============================================================
 * 全局状态
 * ============================================================ */

/** 单帧图像重组状态。 */
legacy_image_rx_t g_legacy_image_rx;

/** 已封装 STM32 帧的全局缓冲。 */
uint8_t g_stm_frame[LEGACY_STM_FRAME_HEADER_SIZE +
                    LEGACY_DEVICE_INFO_SIZE +
                    LEGACY_IMAGE_MAX_SIZE + 1u];

/** 当前 STM32 待发送帧长度，为 0 表示无待发送帧。 */
uint32_t g_stm_frame_length;

/** 当前 STM32 待发送帧已发送偏移。 */
uint32_t g_stm_frame_offset;

/** 当前 STM32 待发送帧的图像字节长度。 */
uint16_t g_stm_image_length;

/** 当前 STM32 待发送帧的图像 ID。 */
uint8_t g_stm_image_id;

/** Radio 接收环形队列描述符。 */
typedef struct
{
    uint8_t data[RADIO_QUEUE_DEPTH][RADIO_PACKET_SIZE]; /** 数据环形缓冲。 */
    volatile uint8_t head;                              /** 主循环读指针。 */
    volatile uint8_t tail;                              /** 中断写指针。 */
    volatile uint32_t received;                         /** CRC 正确的总包数。 */
    volatile uint32_t dropped;                          /** 队列满丢包计数。 */
    uint32_t processed;                                 /** 已处理包数。 */
} receiver_queue_t;

/** Radio 队列实例。 */
static receiver_queue_t m_receiver_queue;

/** Radio EasyDMA 接收缓冲区，按 4 字节对齐。 */
static uint8_t m_receiver_packet[RADIO_PACKET_SIZE] __ALIGNED(4);

/** 上电天线扫描期间为 true；此时 Radio 包只用于质量统计，不进入业务队列。 */
static volatile bool m_antenna_scan_active;

/** 当前天线扫描窗口内检测到的 Radio END 总数。 */
static volatile uint32_t m_antenna_scan_total;

/** 当前天线扫描窗口内 CRC 正确包数。 */
static volatile uint32_t m_antenna_scan_crc_ok;

/** 当前天线扫描窗口内 CRC 正确包的 RSSI 幅值累加值。 */
static volatile uint32_t m_antenna_scan_rssi_sum;

typedef enum
{
    ANTENNA_MODE_DISCOVERY = 0,
    ANTENNA_MODE_TARGET_SCAN,
    ANTENNA_MODE_LOCKED
} antenna_mode_t;

static antenna_mode_t m_antenna_mode;
static uint8_t m_scan_antenna;
static uint8_t m_scan_round;
static uint32_t m_scan_packet_count[RF1662_ANTENNA_COUNT];
static uint32_t m_scan_rssi_sum[RF1662_ANTENNA_COUNT];
/** 最近一次在各天线上收到目标有效包时的RSSI幅值；0表示尚无样本。 */
static volatile uint8_t m_latest_antenna_rssi[RF1662_ANTENNA_COUNT];
static uint8_t m_last_capsule_sn[LEGACY_CAPSULE_SN_SIZE];
static bool m_last_capsule_sn_valid;
static uint8_t m_candidate_antenna[3];
static uint8_t m_candidate_count;
static uint8_t m_candidate_index;
static bool m_scan_request_pending;
static uint8_t m_scan_request_attempts;
static volatile bool m_scan_end_pending;
static volatile uint8_t m_scan_end_frame_id;
static uint32_t m_scan_request_hold_until_ms;
static bool m_fast_scan_start_pending;
static bool m_fast_scan_active;
static uint8_t m_scan_round_limit;
static uint32_t m_scan_dwell_ms;
static uint32_t m_antenna_deadline_ms;
static bool m_discovery_holding;
static volatile uint32_t m_last_target_packet_ms;
static uint32_t m_last_complete_image_ms;
static uint8_t m_failed_frame_count;
static volatile uint32_t m_antenna_time_ms;
static volatile bool m_antenna_service_due;

static void receiver_send_fast_scan_request(uint8_t image_id,
                                            const uint8_t *capsule_sn);

/** 最终有效帧率统计：Radio重组、UART忙丢帧和STM交付分别计数。 */
static uint32_t m_rx_frame_begin_ticks;
static uint32_t m_rx_radio_duration_ms;
static uint32_t m_stm_frame_begin_ticks;
static uint32_t m_stm_queue_start_ticks;
static uint32_t m_stm_radio_duration_ms;
static uint32_t m_last_stm_forward_ticks;
static uint32_t m_complete_frame_count;
static uint32_t m_forwarded_frame_count;
static uint32_t m_uart_busy_drop_count;
static uint32_t m_incomplete_end_count;
static uint32_t m_checksum_failure_count;
static uint32_t m_replaced_frame_count;
static bool m_have_last_stm_forward;

/** RTC2运行在1024Hz；24位差值乘125再除128可精确换算为毫秒。 */
static uint32_t receiver_perf_elapsed_ms(uint32_t start_ticks,
                                         uint32_t end_ticks)
{
    uint32_t elapsed_ticks = (end_ticks - start_ticks) & 0x00FFFFFFu;
    return (elapsed_ticks * 125u) >> 7;
}

/** @brief 判断CRC正确包是否属于当前绑定胶囊。 */
static bool receiver_packet_matches_bound_sn(const uint8_t *packet)
{
    if (!receiver_binding_is_bound())
    {
        return false;
    }
    if ((packet[0] == LEGACY_CMD_CAPSULE_SN_BROADCAST) ||
        (packet[0] == LEGACY_CMD_FAST_SCAN_START))
    {
        return receiver_binding_matches(&packet[1]);
    }
    if ((packet[0] == LEGACY_CMD_IMAGE_BEGIN) ||
        (packet[0] == LEGACY_CMD_IMAGE_DATA) ||
        (packet[0] == LEGACY_CMD_IMAGE_END))
    {
        return receiver_binding_matches(&packet[2]);
    }
    return false;
}

/** @brief 未绑定时只让SN广播和ZAYS配置应答进入软件队列。 */
static bool receiver_unbound_packet_is_needed(const uint8_t *packet)
{
    return (packet[0] == LEGACY_CMD_CAPSULE_SN_BROADCAST) ||
           ((packet[0] == 0x5Au) && (packet[1] == 0x41u) &&
            (packet[2] == 0x59u) && (packet[3] == 0x53u));
}

/** @brief 接收模式统一使用的快捷方式，同时在 ADDRESS 事件启动 RSSI 采样。 */
static uint32_t receiver_radio_rx_shorts(void)
{
    return RADIO_SHORTS_READY_START_Msk |
           RADIO_SHORTS_ADDRESS_RSSISTART_Msk;
}

/** @brief 关闭 Radio 并等待进入 Disabled 状态。 */
static void receiver_radio_disable(void)
{
    if (NRF_RADIO->STATE != RADIO_STATE_STATE_Disabled)
    {
        NRF_RADIO->EVENTS_DISABLED = 0u;
        NRF_RADIO->TASKS_DISABLE = 1u;
        while (NRF_RADIO->EVENTS_DISABLED == 0u) {}
    }
}



/* ============================================================
 * Radio 底层
 * ============================================================ */

/** @brief 设置接收缓冲并启动/继续下一包接收。 */
void receiver_radio_arm(void)
{
    NRF_RADIO->PACKETPTR = (uint32_t)m_receiver_packet;
    NRF_RADIO->EVENTS_END = 0u;
    NRF_RADIO->EVENTS_CRCOK = 0u;
    NRF_RADIO->EVENTS_CRCERROR = 0u;
    if (NRF_RADIO->STATE == RADIO_STATE_STATE_Disabled)
    {
        NRF_RADIO->TASKS_RXEN = 1u;
    }
    else
    {
        NRF_RADIO->TASKS_START = 1u;
    }
}

/** @brief 配置接收板 Radio 参数（与发送板完全一致）。 */
void receiver_radio_init(void)
{
    NRF_RADIO->TXPOWER = RECEIVER_ACK_TX_POWER;
    NRF_RADIO->FREQUENCY = RADIO_FREQUENCY_OFFSET;
    NRF_RADIO->MODE = RADIO_MODE_MODE_Nrf_2Mbit;
    NRF_RADIO->PREFIX0 = 0xC4C3C2E7u;
    NRF_RADIO->PREFIX1 = 0xC5C6C7C8u;
    NRF_RADIO->BASE0 = 0xE7E7E7E7u;
    NRF_RADIO->BASE1 = 0x00C2C2C2u;
    NRF_RADIO->TXADDRESS = 0u;
    NRF_RADIO->RXADDRESSES = 1u;
    NRF_RADIO->PCNF0 = 0u;
    NRF_RADIO->PCNF1 =
        (RADIO_PCNF1_WHITEEN_Disabled << RADIO_PCNF1_WHITEEN_Pos) |
        (RADIO_PCNF1_ENDIAN_Big << RADIO_PCNF1_ENDIAN_Pos) |
        (4u << RADIO_PCNF1_BALEN_Pos) |
        (RADIO_PACKET_SIZE << RADIO_PCNF1_STATLEN_Pos) |
        (RADIO_PACKET_SIZE << RADIO_PCNF1_MAXLEN_Pos);
    NRF_RADIO->CRCCNF = RADIO_CRCCNF_LEN_Two;
    NRF_RADIO->CRCINIT = 0xFFFFu;
    NRF_RADIO->CRCPOLY = 0x11021u;
    NRF_RADIO->SHORTS = receiver_radio_rx_shorts();
    NRF_RADIO->INTENSET = RADIO_INTENSET_END_Msk;
    NVIC_ClearPendingIRQ(RADIO_IRQn);
    NVIC_SetPriority(RADIO_IRQn, RADIO_IRQ_PRIORITY);
    NVIC_EnableIRQ(RADIO_IRQn);
    receiver_radio_arm();
    NRF_LOG_INFO("Dedicated RX RADIO ready: %u MHz, 2 Mbit, legacy packet=%u bytes",
                 (unsigned)RADIO_FREQUENCY_MHZ,
                 (unsigned)RADIO_PACKET_SIZE);
}

/** @brief Radio 收包中断：清 END、检查 CRC、入队、立即恢复接收。 */
void RADIO_IRQHandler(void)
{
    if (NRF_RADIO->EVENTS_END != 0u)
    {
        NRF_RADIO->EVENTS_END = 0u;
        NRF_RADIO->TASKS_RSSISTOP = 1u;

        if ((NRF_RADIO->CRCSTATUS != 0u) &&
            (receiver_packet_matches_bound_sn(m_receiver_packet) ||
             (!receiver_binding_is_bound() &&
              receiver_unbound_packet_is_needed(m_receiver_packet))))
        {
            uint8_t antenna = rf1662_get_antenna();
            if (antenna < RF1662_ANTENNA_COUNT)
            {
                m_latest_antenna_rssi[antenna] =
                    (uint8_t)NRF_RADIO->RSSISAMPLE;
            }
        }

        if ((NRF_RADIO->CRCSTATUS != 0u) &&
            receiver_packet_matches_bound_sn(m_receiver_packet))
        {
            m_last_target_packet_ms = m_antenna_time_ms;
        }

        if (m_antenna_scan_active)
        {
            /* END 自带帧 ID 和绑定 SN；即使没有 BEGIN，也可请求 TX 扫描。 */
            if (!m_fast_scan_active && m_scan_request_pending &&
                (NRF_RADIO->CRCSTATUS != 0u) &&
                (m_receiver_packet[0] == LEGACY_CMD_IMAGE_END) &&
                receiver_packet_matches_bound_sn(m_receiver_packet))
            {
                m_scan_end_frame_id = m_receiver_packet[1];
                m_scan_end_pending = true;
            }
            if (!m_fast_scan_active && m_scan_request_pending &&
                (NRF_RADIO->CRCSTATUS != 0u) &&
                (m_receiver_packet[0] == LEGACY_CMD_FAST_SCAN_START) &&
                receiver_packet_matches_bound_sn(m_receiver_packet))
            {
                m_fast_scan_start_pending = true;
                m_scan_request_pending = false;
                m_scan_request_attempts = 0u;
            }
            ++m_antenna_scan_total;
            if ((NRF_RADIO->CRCSTATUS != 0u) &&
                receiver_packet_matches_bound_sn(m_receiver_packet))
            {
                if (!m_fast_scan_active ||
                    (m_receiver_packet[0] == LEGACY_CMD_CAPSULE_SN_BROADCAST))
                {
                    ++m_antenna_scan_crc_ok;
                    m_antenna_scan_rssi_sum += NRF_RADIO->RSSISAMPLE;
                }
            }
            receiver_radio_arm();
            return;
        }

        if ((NRF_RADIO->CRCSTATUS != 0u) &&
            (receiver_binding_is_bound() ||
             receiver_unbound_packet_is_needed(m_receiver_packet)))
        {
            uint8_t tail = m_receiver_queue.tail;
            uint8_t next = (uint8_t)((tail + 1u) % RADIO_QUEUE_DEPTH);
            ++m_receiver_queue.received;
            if (next != m_receiver_queue.head)
            {
                memcpy(m_receiver_queue.data[tail], m_receiver_packet,
                       RADIO_PACKET_SIZE);
                m_receiver_queue.tail = next;
            }
            else
            {
                ++m_receiver_queue.dropped;
            }
        }
        receiver_radio_arm();
    }
}

/** @brief 保存独立SN广播中的有效字段，不保存Radio填充区。 */
void receiver_device_info_update_capsule_sn(const uint8_t *capsule_sn)
{
    if (capsule_sn == NULL)
    {
        return;
    }
    memcpy(m_last_capsule_sn, capsule_sn, LEGACY_CAPSULE_SN_SIZE);
    m_last_capsule_sn_valid = true;
}

/** @brief 按TYGD31固定偏移生成TX+RX部分设备信息，STM字段留0。 */
static void receiver_build_device_info(uint8_t *device_info)
{
    static const uint8_t marker[LEGACY_DEVICE_INFO_MARKER_SIZE] =
        {0x00u, 0x55u, 0xAAu, 0x88u, 0x99u};
    const uint8_t *capsule_sn = g_legacy_image_rx.capsule_sn;
    const uint8_t *rx_device_id = (const uint8_t *)NRF_FICR->DEVICEID;
    uint8_t antenna;

    memset(device_info, 0, LEGACY_DEVICE_INFO_SIZE);
    memcpy(&device_info[LEGACY_DEVICE_INFO_MARKER_OFFSET], marker,
           sizeof(marker));
    device_info[LEGACY_DEVICE_INFO_TX_VERSION_MAIN_OFFSET] =
        g_legacy_image_rx.version_main;
    device_info[LEGACY_DEVICE_INFO_TX_VERSION_SUB_OFFSET] =
        g_legacy_image_rx.version_sub;
    device_info[LEGACY_DEVICE_INFO_TX_VERSION_TEST_OFFSET] =
        g_legacy_image_rx.version_test;
    device_info[LEGACY_DEVICE_INFO_RX_VERSION_MAIN_OFFSET] = VERSION_MAIN;
    device_info[LEGACY_DEVICE_INFO_RX_VERSION_SUB_OFFSET] = VERSION_SUB;
    device_info[LEGACY_DEVICE_INFO_RX_VERSION_TEST_OFFSET] = VERSION_TEST;

    /* BEGIN中的SN与当前图片严格绑定；匹配时使用已提取的广播缓存。 */
    if (m_last_capsule_sn_valid &&
        (memcmp(m_last_capsule_sn, capsule_sn,
                LEGACY_CAPSULE_SN_SIZE) == 0))
    {
        capsule_sn = m_last_capsule_sn;
    }
    memcpy(&device_info[LEGACY_DEVICE_INFO_CAPSULE_SN_OFFSET], capsule_sn,
           LEGACY_CAPSULE_SN_SIZE);
    memcpy(&device_info[LEGACY_DEVICE_INFO_RX_DEVICE_ID_OFFSET],
           rx_device_id, LEGACY_DEVICE_INFO_RX_DEVICE_ID_SIZE);

    for (antenna = 0u; antenna < 6u; ++antenna)
    {
        uint8_t upper = (uint8_t)(antenna + 6u);
        device_info[LEGACY_DEVICE_INFO_ANT_RSSI_1_6_OFFSET + antenna] =
            m_latest_antenna_rssi[antenna];
        device_info[LEGACY_DEVICE_INFO_ANT_RSSI_7_12_OFFSET + antenna] =
            m_latest_antenna_rssi[upper];
    }
    device_info[LEGACY_DEVICE_INFO_ACTIVE_ANTENNA_OFFSET] =
        (uint8_t)(rf1662_get_antenna() + 1u);
    device_info[LEGACY_DEVICE_INFO_RADIO_FREQUENCY_OFFSET] =
        (uint8_t)RADIO_FREQUENCY_OFFSET;

    device_info[LEGACY_DEVICE_INFO_ACCEL_VALID_OFFSET] =
        g_legacy_image_rx.accel_valid ? 1u : 0u;
    LegacyProtocol_PutI16Be(
        &device_info[LEGACY_DEVICE_INFO_ACCEL_X_OFFSET],
        g_legacy_image_rx.accel_x_raw);
    LegacyProtocol_PutI16Be(
        &device_info[LEGACY_DEVICE_INFO_ACCEL_Y_OFFSET],
        g_legacy_image_rx.accel_y_raw);
    LegacyProtocol_PutI16Be(
        &device_info[LEGACY_DEVICE_INFO_ACCEL_Z_OFFSET],
        g_legacy_image_rx.accel_z_raw);

#if RX_DEVICE_INFO_LOG_ENABLED
    NRF_LOG_INFO("[DEVICE_INFO RX] frame=%u bytes=128 (STM fields still zero)",
                 (unsigned)g_legacy_image_rx.image_id);
    NRF_LOG_HEXDUMP_INFO(device_info, LEGACY_DEVICE_INFO_SIZE);
#endif
}

/** @brief 比较两路扫描结果；成功率优先，其次比较平均 RSSI。 */
static bool receiver_antenna_result_is_better(uint32_t ok,
                                               uint32_t total,
                                               uint32_t average_rssi,
                                               uint32_t best_ok,
                                               uint32_t best_total,
                                               uint32_t best_average_rssi)
{
    uint64_t lhs;
    uint64_t rhs;

    if (total == 0u)
    {
        return false;
    }
    if (best_total == 0u)
    {
        return true;
    }

    /* 交叉相乘比较 ok/total，避免整数百分比截断。 */
    lhs = (uint64_t)ok * best_total;
    rhs = (uint64_t)best_ok * total;
    if (lhs != rhs)
    {
        return lhs > rhs;
    }

    /* nRF RSSISAMPLE 是正的 dBm 幅值，数值越小表示信号越强。 */
    return average_rssi < best_average_rssi;
}

/**
 * @brief 上电扫描12路天线并固定选择有效包平均RSSI最强的一路。
 * @note 每路扫描RF1662_SCAN_DWELL_MS；RSSI相同时比较CRC成功率。
 */
uint8_t receiver_scan_best_antenna(void)
{
    uint8_t antenna;
    uint8_t best_antenna = RF1662_DEFAULT_ANTENNA;
    uint32_t best_ok = 0u;
    uint32_t best_total = 0u;
    uint32_t best_average_rssi = 0xFFFFFFFFu;

    NRF_LOG_INFO("RF1662 startup scan: 12 antennas, dwell=%u ms each",
                 (unsigned)RF1662_SCAN_DWELL_MS);
    NRF_LOG_FLUSH();

    m_antenna_scan_active = true;
    m_scan_end_pending = false;
    m_scan_request_hold_until_ms = 0u;
    for (antenna = 0u; antenna < RF1662_ANTENNA_COUNT; ++antenna)
    {
        uint32_t total;
        uint32_t ok;
        uint32_t rssi_sum;
        uint32_t average_rssi;
        uint32_t quality_permille;

        NVIC_DisableIRQ(RADIO_IRQn);
        receiver_radio_disable();
        (void)rf1662_select_antenna(antenna);
        m_antenna_scan_total = 0u;
        m_antenna_scan_crc_ok = 0u;
        m_antenna_scan_rssi_sum = 0u;
        NRF_RADIO->EVENTS_END = 0u;
        NRF_RADIO->EVENTS_CRCOK = 0u;
        NRF_RADIO->EVENTS_CRCERROR = 0u;
        NRF_RADIO->SHORTS = receiver_radio_rx_shorts();
        receiver_radio_arm();
        NVIC_ClearPendingIRQ(RADIO_IRQn);
        NVIC_EnableIRQ(RADIO_IRQn);

        /* Radio 中断在忙等待期间持续采样，但扫描包不会进入业务队列。 */
        nrf_delay_ms(RF1662_SCAN_DWELL_MS);

        NVIC_DisableIRQ(RADIO_IRQn);
        receiver_radio_disable();
        total = m_antenna_scan_total;
        ok = m_antenna_scan_crc_ok;
        rssi_sum = m_antenna_scan_rssi_sum;
        average_rssi = (ok != 0u) ? (rssi_sum / ok) : 0xFFFFFFFFu;
        quality_permille = (total != 0u) ? ((ok * 1000u) / total) : 0u;

        NRF_LOG_INFO("RF1662 scan ANT%u: crc=%u/%u quality=%u.%u%% avg_rssi=-%u dBm",
                     (unsigned)(antenna + 1u),
                     (unsigned)ok,
                     (unsigned)total,
                     (unsigned)(quality_permille / 10u),
                     (unsigned)(quality_permille % 10u),
                     (unsigned)((ok != 0u) ? average_rssi : 0u));
        NRF_LOG_FLUSH();

        if (receiver_antenna_result_is_better(ok, total, average_rssi,
                                              best_ok, best_total,
                                              best_average_rssi))
        {
            best_antenna = antenna;
            best_ok = ok;
            best_total = total;
            best_average_rssi = average_rssi;
        }
    }

    (void)rf1662_select_antenna(best_antenna);
    memset(&m_receiver_queue, 0, sizeof(m_receiver_queue));
    memset(&g_legacy_image_rx, 0, sizeof(g_legacy_image_rx));
    m_antenna_scan_active = false;
    NRF_RADIO->EVENTS_END = 0u;
    NRF_RADIO->EVENTS_CRCOK = 0u;
    NRF_RADIO->EVENTS_CRCERROR = 0u;
    NRF_RADIO->SHORTS = receiver_radio_rx_shorts();
    receiver_radio_arm();
    NVIC_ClearPendingIRQ(RADIO_IRQn);
    NVIC_EnableIRQ(RADIO_IRQn);

    NRF_LOG_INFO("RF1662 selected ANT%u: crc=%u/%u avg_rssi=-%u dBm",
                 (unsigned)(best_antenna + 1u),
                 (unsigned)best_ok,
                 (unsigned)best_total,
                 (unsigned)((best_ok != 0u) ? best_average_rssi : 0u));
    NRF_LOG_FLUSH();
    return best_antenna;
}

/** @brief 在主循环中安全切换天线，并恢复Radio连续接收。 */
static void receiver_antenna_switch(uint8_t antenna)
{
    NVIC_DisableIRQ(RADIO_IRQn);
    receiver_radio_disable();
    (void)rf1662_select_antenna(antenna);
    NRF_RADIO->EVENTS_END = 0u;
    NRF_RADIO->EVENTS_CRCOK = 0u;
    NRF_RADIO->EVENTS_CRCERROR = 0u;
    NRF_RADIO->SHORTS = receiver_radio_rx_shorts();
    receiver_radio_arm();
    NVIC_ClearPendingIRQ(RADIO_IRQn);
    NVIC_EnableIRQ(RADIO_IRQn);
}

static void receiver_antenna_start_discovery(void)
{
    m_antenna_scan_active = false;
    m_antenna_mode = ANTENNA_MODE_DISCOVERY;
    m_scan_antenna = RF1662_DEFAULT_ANTENNA;
    m_discovery_holding = false;
    memset(&g_legacy_image_rx, 0, sizeof(g_legacy_image_rx));
    receiver_antenna_switch(m_scan_antenna);
    m_antenna_deadline_ms = m_antenna_time_ms + RF1662_DISCOVERY_DWELL_MS;
    NRF_LOG_INFO("RF1662 mode=DISCOVERY ANT%u", (unsigned)(m_scan_antenna + 1u));
}

static void receiver_antenna_start_target_scan(bool fast)
{
    m_fast_scan_active = fast;
    m_scan_dwell_ms = fast ? RF1662_FAST_SCAN_DWELL_MS : RF1662_TARGET_SCAN_DWELL_MS;
    m_scan_round_limit = fast ? RF1662_FAST_SCAN_ROUNDS : RF1662_TARGET_SCAN_ROUNDS;
    m_antenna_mode = ANTENNA_MODE_TARGET_SCAN;
    m_antenna_scan_active = true;
    m_scan_end_pending = false;
    m_scan_request_hold_until_ms = 0u;
    m_scan_antenna = 0u;
    m_scan_round = 0u;
    memset(m_scan_packet_count, 0, sizeof(m_scan_packet_count));
    memset(m_scan_rssi_sum, 0, sizeof(m_scan_rssi_sum));
    memset((void *)m_latest_antenna_rssi, 0,
           sizeof(m_latest_antenna_rssi));
    memset(m_candidate_antenna, 0, sizeof(m_candidate_antenna));
    m_candidate_count = 0u;
    m_candidate_index = 0u;
    m_antenna_scan_total = 0u;
    m_antenna_scan_crc_ok = 0u;
    m_antenna_scan_rssi_sum = 0u;
    memset(&m_receiver_queue, 0, sizeof(m_receiver_queue));
    memset(&g_legacy_image_rx, 0, sizeof(g_legacy_image_rx));
    receiver_antenna_switch(m_scan_antenna);
    m_antenna_deadline_ms = m_antenna_time_ms + m_scan_dwell_ms;
    NRF_LOG_INFO("RF1662 mode=TARGET_SCAN dwell=%ums rounds=%u",
                 (unsigned)m_scan_dwell_ms,
                 (unsigned)m_scan_round_limit);
}

/** @brief RTC2提供50ms状态机节拍，不在中断中切换射频通路。 */
void RTC2_IRQHandler(void)
{
    if (NRF_RTC2->EVENTS_COMPARE[0] != 0u)
    {
        NRF_RTC2->EVENTS_COMPARE[0] = 0u;
        NRF_RTC2->CC[0] = (NRF_RTC2->COUNTER +
                           ((1024u * RF1662_SERVICE_TICK_MS) / 1000u)) &
                          0x00FFFFFFu;
        m_antenna_time_ms += RF1662_SERVICE_TICK_MS;
        m_antenna_service_due = true;
    }
}

void receiver_antenna_manager_init(void)
{
    NRF_CLOCK->LFCLKSRC = CLOCK_LFCLKSRC_SRC_RC;
    NRF_CLOCK->EVENTS_LFCLKSTARTED = 0u;
    NRF_CLOCK->TASKS_LFCLKSTART = 1u;
    while (NRF_CLOCK->EVENTS_LFCLKSTARTED == 0u) {}

    NRF_RTC2->TASKS_STOP = 1u;
    NRF_RTC2->TASKS_CLEAR = 1u;
    NRF_RTC2->PRESCALER = 31u; /* 32768/(31+1)=1024 Hz */
    NRF_RTC2->CC[0] = (1024u * RF1662_SERVICE_TICK_MS) / 1000u;
    NRF_RTC2->EVENTS_COMPARE[0] = 0u;
    NRF_RTC2->INTENSET = RTC_INTENSET_COMPARE0_Msk;
    NVIC_ClearPendingIRQ(RTC2_IRQn);
    NVIC_SetPriority(RTC2_IRQn, 7u);
    NVIC_EnableIRQ(RTC2_IRQn);
    NRF_RTC2->TASKS_START = 1u;
    receiver_antenna_start_discovery();
}

void receiver_antenna_binding_changed(void)
{
    m_scan_end_pending = false;
    m_scan_request_hold_until_ms = 0u;
    /* 新绑定不能沿用上一设备或发现阶段留下的天线质量。 */
    memset((void *)m_latest_antenna_rssi, 0,
           sizeof(m_latest_antenna_rssi));
    if (receiver_binding_is_bound())
    {
        m_scan_request_pending = true;
        m_scan_request_attempts = 0u;
        m_fast_scan_start_pending = false;
        receiver_antenna_start_target_scan(false);
    }
    else
    {
        m_scan_request_pending = false;
        m_fast_scan_start_pending = false;
        memset(m_last_capsule_sn, 0, sizeof(m_last_capsule_sn));
        m_last_capsule_sn_valid = false;
        receiver_antenna_start_discovery();
    }
}

void receiver_antenna_fast_scan_granted(const uint8_t *sn)
{
    if (receiver_binding_is_bound() && m_scan_request_pending &&
        receiver_binding_matches(sn))
    {
        m_scan_request_pending = false;
        m_scan_request_attempts = 0u;
        m_fast_scan_start_pending = true;
    }
}

void receiver_antenna_note_complete_image(void)
{
    if ((m_antenna_mode == ANTENNA_MODE_LOCKED) &&
        receiver_binding_is_bound())
    {
        m_last_complete_image_ms = m_antenna_time_ms;
        m_last_target_packet_ms = m_antenna_time_ms;
        m_failed_frame_count = 0u;
    }
}

void receiver_antenna_note_discovery_sn(void)
{
    if ((m_antenna_mode == ANTENNA_MODE_DISCOVERY) &&
        !m_discovery_holding)
    {
        m_discovery_holding = true;
        m_antenna_deadline_ms = m_antenna_time_ms +
                                RF1662_DISCOVERY_FOUND_HOLD_MS;
        NRF_LOG_INFO("RF1662 discovery hold ANT%u for %ums",
                     (unsigned)(m_scan_antenna + 1u),
                     (unsigned)RF1662_DISCOVERY_FOUND_HOLD_MS);
    }
}

/** @brief 比较扫描候选：足够样本优先，然后选平均RSSI更强的一路。 */
static bool receiver_scan_candidate_is_better(uint8_t antenna, uint8_t old)
{
    uint32_t count = m_scan_packet_count[antenna];
    uint32_t old_count = m_scan_packet_count[old];
    uint32_t average = m_scan_rssi_sum[antenna] / count;
    uint32_t old_average = m_scan_rssi_sum[old] / old_count;
    bool reliable = count >= RF1662_MIN_RSSI_SAMPLES;
    bool old_reliable = old_count >= RF1662_MIN_RSSI_SAMPLES;

    if (reliable != old_reliable)
    {
        return reliable;
    }
    /* RSSISAMPLE是负dBm的幅值；数值越小，实际信号越强。 */
    if (average != old_average)
    {
        return average < old_average;
    }
    return count > old_count;
}

/** @brief 根据多轮目标包的平均RSSI生成前三名候选天线。 */
static void receiver_antenna_build_candidates(void)
{
    uint8_t antenna;
    uint8_t position;

    m_candidate_count = 0u;
    /* 扫描值保留到下次扫描；没有有效样本的通道明确记为0。 */
    for (antenna = 0u; antenna < RF1662_ANTENNA_COUNT; ++antenna)
    {
        m_latest_antenna_rssi[antenna] =
            (m_scan_packet_count[antenna] != 0u) ?
            (uint8_t)(m_scan_rssi_sum[antenna] /
                      m_scan_packet_count[antenna]) : 0u;
    }
    for (antenna = 0u; antenna < RF1662_ANTENNA_COUNT; ++antenna)
    {
        if (m_scan_packet_count[antenna] == 0u)
        {
            continue;
        }
        for (position = 0u; position < m_candidate_count; ++position)
        {
            uint8_t old = m_candidate_antenna[position];
            if (receiver_scan_candidate_is_better(antenna, old))
            {
                break;
            }
        }
        if (position < 3u)
        {
            uint8_t move = (m_candidate_count < 3u) ? m_candidate_count : 2u;
            while (move > position)
            {
                m_candidate_antenna[move] = m_candidate_antenna[move - 1u];
                --move;
            }
            m_candidate_antenna[position] = antenna;
            if (m_candidate_count < 3u)
            {
                ++m_candidate_count;
            }
        }
    }
    for (position = 0u; position < m_candidate_count; ++position)
    {
        antenna = m_candidate_antenna[position];
        NRF_LOG_INFO("RF1662 candidate %u ANT%u packets=%u avg_rssi=-%u dBm",
                     (unsigned)(position + 1u), (unsigned)(antenna + 1u),
                     (unsigned)m_scan_packet_count[antenna],
                     (unsigned)(m_scan_rssi_sum[antenna] /
                                m_scan_packet_count[antenna]));
    }
}

/** @brief 锁定指定候选，并重新开始链路健康计时。 */
static void receiver_antenna_lock_candidate(uint8_t candidate_index)
{
    uint8_t antenna = m_candidate_antenna[candidate_index];
    m_antenna_scan_active = false;
    m_antenna_mode = ANTENNA_MODE_LOCKED;
    m_candidate_index = candidate_index;
    memset(&m_receiver_queue, 0, sizeof(m_receiver_queue));
    memset(&g_legacy_image_rx, 0, sizeof(g_legacy_image_rx));
    receiver_antenna_switch(antenna);
    m_last_target_packet_ms = m_antenna_time_ms;
    m_last_complete_image_ms = m_antenna_time_ms;
    m_failed_frame_count = 0u;
    NRF_LOG_INFO("RF1662 LOCKED candidate=%u/%u ANT%u packets=%u avg_rssi=-%u dBm",
                 (unsigned)(candidate_index + 1u),
                 (unsigned)m_candidate_count,
                 (unsigned)(antenna + 1u),
                 (unsigned)m_scan_packet_count[antenna],
                 (unsigned)(m_scan_rssi_sum[antenna] /
                            m_scan_packet_count[antenna]));
}

bool receiver_antenna_service(void)
{
    uint32_t now;
    uint32_t packet_count;

    if (m_fast_scan_start_pending)
    {
        m_fast_scan_start_pending = false;
        receiver_antenna_start_target_scan(true);
        return true;
    }
    if (m_scan_end_pending)
    {
        uint8_t image_id = m_scan_end_frame_id;
        m_scan_end_pending = false;
        if (m_scan_request_pending && receiver_binding_is_bound())
        {
            if (m_scan_request_attempts < 2u)
            {
                receiver_send_fast_scan_request(image_id,
                                                receiver_binding_get());
                /* 停在捕获 END 的天线上，等待 TX 的开始通知。 */
                m_scan_request_hold_until_ms = m_antenna_time_ms + 150u;
            }
            else
            {
                m_scan_request_pending = false;
            }
        }
        return true;
    }
    if (!m_antenna_service_due)
    {
        return false;
    }
    m_antenna_service_due = false;
    now = m_antenna_time_ms;

    if (m_antenna_mode == ANTENNA_MODE_DISCOVERY)
    {
        if (receiver_binding_is_bound())
        {
            receiver_antenna_start_target_scan(false);
        }
        else if ((int32_t)(now - m_antenna_deadline_ms) >= 0)
        {
            m_discovery_holding = false;
            m_scan_antenna = (uint8_t)((m_scan_antenna + 1u) % RF1662_ANTENNA_COUNT);
            receiver_antenna_switch(m_scan_antenna);
            m_antenna_deadline_ms = now + RF1662_DISCOVERY_DWELL_MS;
            NRF_LOG_INFO("RF1662 discovery ANT%u", (unsigned)(m_scan_antenna + 1u));
        }
        return true;
    }

    if (!receiver_binding_is_bound())
    {
        receiver_antenna_start_discovery();
        return true;
    }

    if (m_antenna_mode == ANTENNA_MODE_TARGET_SCAN)
    {
        if ((int32_t)(now - m_scan_request_hold_until_ms) < 0)
        {
            return false;
        }
        if ((int32_t)(now - m_antenna_deadline_ms) < 0)
        {
            return false;
        }
        packet_count = m_antenna_scan_crc_ok;
        m_scan_packet_count[m_scan_antenna] += packet_count;
        m_scan_rssi_sum[m_scan_antenna] += m_antenna_scan_rssi_sum;
        m_antenna_scan_total = 0u;
        m_antenna_scan_crc_ok = 0u;
        m_antenna_scan_rssi_sum = 0u;

        ++m_scan_antenna;
        if (m_scan_antenna >= RF1662_ANTENNA_COUNT)
        {
            m_scan_antenna = 0u;
            ++m_scan_round;
        }

        if (m_scan_round < m_scan_round_limit)
        {
            receiver_antenna_switch(m_scan_antenna);
            m_antenna_deadline_ms = now + m_scan_dwell_ms;
        }
        else
        {
            receiver_antenna_build_candidates();
            if (m_candidate_count != 0u)
            {
                receiver_antenna_lock_candidate(0u);
            }
            else
            {
                NRF_LOG_WARNING("RF1662 target not found; restarting scan");
                receiver_antenna_start_target_scan(false);
            }
        }
        return true;
    }

    if ((m_antenna_mode == ANTENNA_MODE_LOCKED) &&
        (((uint32_t)(now - m_last_target_packet_ms) >=
          RF1662_TARGET_PACKET_TIMEOUT_MS) ||
         ((uint32_t)(now - m_last_complete_image_ms) >=
          RF1662_COMPLETE_IMAGE_TIMEOUT_MS) ||
         (m_failed_frame_count >= RF1662_FAILED_FRAME_LIMIT)))
    {
        g_legacy_image_rx.active = false;
        if ((uint8_t)(m_candidate_index + 1u) < m_candidate_count)
        {
            NRF_LOG_WARNING("RF1662 link weak; trying next candidate");
            receiver_antenna_lock_candidate((uint8_t)(m_candidate_index + 1u));
        }
        else
        {
            NRF_LOG_WARNING("RF1662 candidates exhausted; seeking image END");
            m_scan_request_pending = true;
            m_scan_request_attempts = 0u;
            receiver_antenna_start_target_scan(false);
        }
        return true;
    }
    return false;
}

/** @brief 从环形队列取出一包并交给协议解析器。 */
bool receiver_forward_one(void)
{
    uint8_t head;
    if (m_receiver_queue.head == m_receiver_queue.tail)
    {
        return false;
    }
    head = m_receiver_queue.head;
    if (!receiver_control_handle_radio_packet(m_receiver_queue.data[head]))
    {
        receiver_process_legacy_packet(m_receiver_queue.data[head]);
    }
    m_receiver_queue.head = (uint8_t)((head + 1u) % RADIO_QUEUE_DEPTH);
    ++m_receiver_queue.processed;
    if ((m_receiver_queue.processed & 0x1Fu) == 0u)
    {
        NRF_LOG_INFO("Bridge stats: radio_rx=%u radio_processed=%u dropped=%u stm_pending=%u",
                     (unsigned)m_receiver_queue.received,
                     (unsigned)m_receiver_queue.processed,
                     (unsigned)m_receiver_queue.dropped,
                     (unsigned)(g_stm_frame_length != 0u));
    }
    return true;
}



/* ============================================================
 * 协议解析与转发
 * ============================================================ */

/** @brief 利用 END 后的接收窗口请求 TX 暂停图片并快速广播 SN。 */
static void receiver_send_fast_scan_request(uint8_t image_id,
                                            const uint8_t *capsule_sn)
{
    uint8_t request[RADIO_PACKET_SIZE] __ALIGNED(4) = {0};
    request[0] = LEGACY_CMD_FAST_SCAN_REQUEST;
    request[1] = image_id;
    memcpy(&request[2], capsule_sn, LEGACY_CAPSULE_SN_SIZE);
    ++m_scan_request_attempts;
    receiver_send_control_packet(request, RADIO_PACKET_SIZE, 2u);
    NRF_LOG_INFO("[SCAN] request sent for frame=%u attempt=%u",
                 (unsigned)image_id, (unsigned)m_scan_request_attempts);
}

/** @brief 正常 ACK；待协商时使用该帧的应答窗口发送扫描请求。 */
void receiver_send_image_ack(uint8_t image_id)
{
    uint8_t response[RADIO_PACKET_SIZE] __ALIGNED(4) = {0};
    uint8_t  repeat;
    uint32_t wait_count;

    if (m_scan_request_pending)
    {
        if (m_scan_request_attempts < 2u)
        {
            receiver_send_fast_scan_request(image_id,
                                            g_legacy_image_rx.capsule_sn);
            return;
        }
        m_scan_request_pending = false;
    }

    response[0] = LEGACY_CMD_IMAGE_RECEIVED_RESPONSE;
    response[1] = image_id;
    memcpy(&response[2], g_legacy_image_rx.capsule_sn,
           LEGACY_CAPSULE_SN_SIZE);

    /* 短暂暂停接收，发送 ACK 后立即恢复 RX。 */
    NVIC_DisableIRQ(RADIO_IRQn);

    /* 等待 Radio 进入 Disabled 状态（带超时保护） */
    NRF_RADIO->EVENTS_DISABLED = 0u;
    NRF_RADIO->TASKS_DISABLE = 1u;
    wait_count = 0u;
    while (NRF_RADIO->EVENTS_DISABLED == 0u)
    {
        wait_count++;
        if (wait_count > 100000u)
        {
            NRF_LOG_ERROR("[ACK] Radio disable TIMEOUT");
            break;
        }
    }
    nrf_gpio_pin_set(RECEIVER_MODE_PIN);

    NRF_RADIO->PACKETPTR = (uint32_t)response;
    NRF_RADIO->SHORTS = RADIO_SHORTS_READY_START_Msk |
                        RADIO_SHORTS_END_DISABLE_Msk;

    /* 连发 2 次 ACK（每帧发送两次抗丢包） */
    for (repeat = 0u; repeat < 2u; repeat++)
    {
        NRF_RADIO->EVENTS_DISABLED = 0u;
        NRF_RADIO->TASKS_TXEN = 1u;
        wait_count = 0u;
        while (NRF_RADIO->EVENTS_DISABLED == 0u)
        {
            wait_count++;
            if (wait_count > 100000u)
            {
                NRF_LOG_ERROR("[ACK] TXEN #%u TIMEOUT", (unsigned)repeat);
                break;
            }
        }
    }

    nrf_gpio_pin_clear(RECEIVER_MODE_PIN);

    NRF_RADIO->SHORTS = receiver_radio_rx_shorts();
    receiver_radio_arm();
    NVIC_ClearPendingIRQ(RADIO_IRQn);
    NVIC_EnableIRQ(RADIO_IRQn);
    NRF_LOG_INFO("[ACK] frame=%u sent x2", (unsigned)image_id);
}

/**
 * @brief 暂停接收并把短控制请求通过Radio重复发送给TX，随后恢复接收。
 * @note 由UART控制路由调用，不用于图片ACK。
 */
void receiver_send_control_packet(const uint8_t *packet, uint16_t length,
                                  uint8_t repeat_count)
{
    uint8_t repeat;
    uint8_t tx_packet[RADIO_PACKET_SIZE] __ALIGNED(4) = {0};
    if ((packet == NULL) || (length == 0u) ||
        (length > RADIO_PACKET_SIZE) || (repeat_count == 0u))
    {
        return;
    }
    memcpy(tx_packet, packet, length);
    NVIC_DisableIRQ(RADIO_IRQn);
    receiver_radio_disable();
    nrf_gpio_pin_set(RECEIVER_MODE_PIN);
    NRF_RADIO->PACKETPTR = (uint32_t)tx_packet;
    NRF_RADIO->SHORTS = RADIO_SHORTS_READY_START_Msk |
                        RADIO_SHORTS_END_DISABLE_Msk;
    for (repeat = 0u; repeat < repeat_count; ++repeat)
    {
        NRF_RADIO->EVENTS_DISABLED = 0u;
        NRF_RADIO->TASKS_TXEN = 1u;
        while (NRF_RADIO->EVENTS_DISABLED == 0u) {}
    }
    nrf_gpio_pin_clear(RECEIVER_MODE_PIN);
    NRF_RADIO->SHORTS = receiver_radio_rx_shorts();
    receiver_radio_arm();
    NVIC_ClearPendingIRQ(RADIO_IRQn);
    NVIC_EnableIRQ(RADIO_IRQn);
}

/** @brief 将已校验图片封装为 STM32 帧并加入异步 UART 发送队列。 */
bool receiver_forward_complete_image(void)
{
    uint16_t payload_length = (uint16_t)(LEGACY_DEVICE_INFO_SIZE +
                                         g_legacy_image_rx.image_length);
    uint16_t checksum_index;
    uint8_t checksum = 0u;
    uint32_t frame_length = LEGACY_STM_FRAME_HEADER_SIZE + payload_length + 1u;

    if (g_stm_frame_length != 0u)
    {
        ++m_uart_busy_drop_count;
        NRF_LOG_WARNING("STM UART busy; image %u not queued",
                        g_legacy_image_rx.image_id);
        return false;
    }

    memset(g_stm_frame, 0, frame_length);
    g_stm_frame[0] = 0xFFu;
    g_stm_frame[1] = 0x55u;
    g_stm_frame[2] = 0x12u;
    g_stm_frame[3] = 0x34u;
    g_stm_frame[4] = LEGACY_CMD_IMAGE_FORWARD;
    LegacyProtocol_PutU16Be(&g_stm_frame[5], payload_length);
    receiver_build_device_info(
        &g_stm_frame[LEGACY_STM_FRAME_HEADER_SIZE]);
    memcpy(&g_stm_frame[LEGACY_STM_FRAME_HEADER_SIZE +
                        LEGACY_DEVICE_INFO_SIZE],
           g_legacy_image_rx.image, g_legacy_image_rx.image_length);
    /* RX->STM校验覆盖设备信息和JPEG，防止SN/版本/RSSI静默损坏。 */
    for (checksum_index = 0u;
         checksum_index < payload_length;
         checksum_index++)
    {
        checksum = (uint8_t)(checksum +
            g_stm_frame[LEGACY_STM_FRAME_HEADER_SIZE + checksum_index]);
    }
    g_stm_frame[frame_length - 1u] = checksum;
    g_stm_frame_offset = 0u;
    g_stm_frame_length = frame_length;
    g_stm_image_length = g_legacy_image_rx.image_length;
    g_stm_image_id = g_legacy_image_rx.image_id;
    m_stm_frame_begin_ticks = m_rx_frame_begin_ticks;
    m_stm_queue_start_ticks = NRF_RTC2->COUNTER;
    m_stm_radio_duration_ms = m_rx_radio_duration_ms;
    return true;
}

/** @brief 图片最后一个UART字节实际发完后计算最终有效帧率。 */
void receiver_note_stm_forwarded(uint8_t image_id, uint16_t image_length)
{
    uint32_t now_ticks = NRF_RTC2->COUNTER;
    uint32_t uart_ms = receiver_perf_elapsed_ms(m_stm_queue_start_ticks,
                                                now_ticks);
    uint32_t total_ms = receiver_perf_elapsed_ms(m_stm_frame_begin_ticks,
                                                 now_ticks);

    ++m_forwarded_frame_count;
#if RX_FRAME_RATE_LOG_ENABLED
    if (m_have_last_stm_forward)
    {
        uint32_t interval_ms = receiver_perf_elapsed_ms(
            m_last_stm_forward_ticks, now_ticks);
        uint32_t fps_x100 = (interval_ms != 0u) ?
                            (100000u / interval_ms) : 0u;
        NRF_LOG_INFO("[FPS] STM frame=%u interval=%ums rate=%u.%02u fps",
                     (unsigned)image_id, (unsigned)interval_ms,
                     (unsigned)(fps_x100 / 100u),
                     (unsigned)(fps_x100 % 100u));
    }
    else
    {
        NRF_LOG_INFO("[FPS] STM frame=%u first complete frame",
                     (unsigned)image_id);
    }
    NRF_LOG_INFO("[PERF] frame=%u radio=%ums uart=%ums total=%ums",
                 (unsigned)image_id, (unsigned)m_stm_radio_duration_ms,
                 (unsigned)uart_ms, (unsigned)total_ms);
    NRF_LOG_INFO("[PERF] bytes=%u complete=%u forwarded=%u busy_drop=%u",
                 (unsigned)image_length, (unsigned)m_complete_frame_count,
                 (unsigned)m_forwarded_frame_count,
                 (unsigned)m_uart_busy_drop_count);
    if ((m_forwarded_frame_count % 10u) == 0u)
    {
        NRF_LOG_INFO("[PERF] loss: incomplete=%u checksum=%u replaced=%u",
                     (unsigned)m_incomplete_end_count,
                     (unsigned)m_checksum_failure_count,
                     (unsigned)m_replaced_frame_count);
    }
#else
    (void)image_id;
    (void)image_length;
#endif
    m_last_stm_forward_ticks = now_ticks;
    m_have_last_stm_forward = true;
}

/** @brief 解析一个原始 TYGD31 Radio 包并推进图片重组状态机。 */
void receiver_process_legacy_packet(const uint8_t *packet)
{
    uint16_t packet_index;
    uint16_t copy_length;
    uint16_t offset;
    uint16_t index;
    uint8_t checksum = 0u;

    if ((packet[0] == LEGACY_CMD_IMAGE_BEGIN) ||
        (packet[0] == LEGACY_CMD_IMAGE_DATA) ||
        (packet[0] == LEGACY_CMD_IMAGE_END))
    {
        if (!receiver_binding_matches(&packet[2]))
        {
            if (packet[0] == LEGACY_CMD_IMAGE_BEGIN)
            {
                NRF_LOG_INFO("Image ignored: RX unbound or SN mismatch");
            }
            return;
        }
    }

    if (packet[0] == LEGACY_CMD_IMAGE_BEGIN)
    {
        uint16_t image_length = LegacyProtocol_GetU16Be(&packet[10]);
        uint16_t packet_count = LegacyProtocol_GetU16Be(&packet[12]);
        uint16_t maximum_packets = (uint16_t)((LEGACY_IMAGE_MAX_SIZE +
            LEGACY_IMAGE_PACKET_PAYLOAD_SIZE - 1u) /
            LEGACY_IMAGE_PACKET_PAYLOAD_SIZE);
        if ((image_length == 0u) || (image_length > LEGACY_IMAGE_MAX_SIZE) ||
            (packet_count == 0u) || (packet_count > maximum_packets) ||
            (packet_count != (uint16_t)((image_length +
             LEGACY_IMAGE_PACKET_PAYLOAD_SIZE - 1u) /
             LEGACY_IMAGE_PACKET_PAYLOAD_SIZE)))
        {
            NRF_LOG_WARNING("Legacy begin rejected: length=%u packets=%u",
                            (unsigned)image_length, (unsigned)packet_count);
            return;
        }
        if (g_legacy_image_rx.active &&
            (packet[1] != g_legacy_image_rx.image_id))
        {
            if (m_failed_frame_count < 0xFFu)
            {
                ++m_failed_frame_count;
            }
            ++m_replaced_frame_count;
        }
        memset(&g_legacy_image_rx, 0, sizeof(g_legacy_image_rx));
        g_legacy_image_rx.active = true;
        g_legacy_image_rx.image_id = packet[1];
        memcpy(g_legacy_image_rx.capsule_sn, &packet[2],
               LEGACY_CAPSULE_SN_SIZE);
        g_legacy_image_rx.image_length = image_length;
        g_legacy_image_rx.packet_count = packet_count;
        m_rx_frame_begin_ticks = NRF_RTC2->COUNTER;
        g_legacy_image_rx.version_main = packet[14];              // 胶囊固件主版本号
        g_legacy_image_rx.version_sub  = packet[15];              // 胶囊固件子版本号
        g_legacy_image_rx.version_test = packet[16];              // 胶囊固件测试版本号
        g_legacy_image_rx.accel_valid =
            (packet[LEGACY_BEGIN_ACCEL_VALID_OFFSET] == 1u);
        g_legacy_image_rx.accel_x_raw =
            LegacyProtocol_GetI16Be(&packet[LEGACY_BEGIN_ACCEL_X_OFFSET]);
        g_legacy_image_rx.accel_y_raw =
            LegacyProtocol_GetI16Be(&packet[LEGACY_BEGIN_ACCEL_Y_OFFSET]);
        g_legacy_image_rx.accel_z_raw =
            LegacyProtocol_GetI16Be(&packet[LEGACY_BEGIN_ACCEL_Z_OFFSET]);
        NRF_LOG_INFO("Legacy image begin: id=%u length=%u packets=%u ver=%u.%u.%u",
                     packet[1], (unsigned)image_length, (unsigned)packet_count,
                     (unsigned)packet[14], (unsigned)packet[15], (unsigned)packet[16]);
        NRF_LOG_INFO("ADXL362 raw: valid=%u X=%d Y=%d Z=%d",
                     g_legacy_image_rx.accel_valid ? 1u : 0u,
                     (int)g_legacy_image_rx.accel_x_raw,
                     (int)g_legacy_image_rx.accel_y_raw,
                     (int)g_legacy_image_rx.accel_z_raw);
        return;
    }
    if ((packet[0] == LEGACY_CMD_IMAGE_END) &&
        m_scan_request_pending &&
        (!g_legacy_image_rx.active ||
         (packet[1] != g_legacy_image_rx.image_id)))
    {
        if (m_scan_request_attempts < 2u)
        {
            receiver_send_fast_scan_request(packet[1], &packet[2]);
        }
        else
        {
            m_scan_request_pending = false;
        }
        return;
    }
    if (!g_legacy_image_rx.active ||
        (packet[1] != g_legacy_image_rx.image_id) ||
        (memcmp(&packet[2], g_legacy_image_rx.capsule_sn,
                LEGACY_CAPSULE_SN_SIZE) != 0))
    {
        return;
    }
    if (packet[0] == LEGACY_CMD_IMAGE_DATA)
    {
        packet_index = LegacyProtocol_GetU16Be(&packet[10]);
        if ((packet_index >= g_legacy_image_rx.packet_count) ||
            (g_legacy_image_rx.received_map[packet_index] != 0u))
        {
            return;
        }
        offset = (uint16_t)(packet_index * LEGACY_IMAGE_PACKET_PAYLOAD_SIZE);
        copy_length = (uint16_t)(g_legacy_image_rx.image_length - offset);
        if (copy_length > LEGACY_IMAGE_PACKET_PAYLOAD_SIZE)
        {
            copy_length = LEGACY_IMAGE_PACKET_PAYLOAD_SIZE;
        }
        memcpy(&g_legacy_image_rx.image[offset],
               &packet[LEGACY_IMAGE_PACKET_HEADER_SIZE], copy_length);
        g_legacy_image_rx.received_map[packet_index] = 1u;
        g_legacy_image_rx.received_count++;
        return;
    }
    if (packet[0] == LEGACY_CMD_IMAGE_END)
    {
        if (g_legacy_image_rx.received_count != g_legacy_image_rx.packet_count)
        {
            ++m_incomplete_end_count;
            NRF_LOG_WARNING("Legacy image incomplete: id=%u received=%u/%u",
                            packet[1], (unsigned)g_legacy_image_rx.received_count,
                            (unsigned)g_legacy_image_rx.packet_count);
            if (m_scan_request_pending)
            {
                receiver_send_image_ack(packet[1]);
            }
            return;
        }
        for (index = 0u; index < g_legacy_image_rx.image_length; index++)
        {
            checksum = (uint8_t)(checksum + g_legacy_image_rx.image[index]);
        }
        if (checksum != packet[10])
        {
            ++m_checksum_failure_count;
            NRF_LOG_WARNING("Legacy image checksum failed: id=%u", packet[1]);
            if (m_failed_frame_count < 0xFFu)
            {
                ++m_failed_frame_count;
            }
            g_legacy_image_rx.active = false;
            return;
        }
        ++m_complete_frame_count;
        m_rx_radio_duration_ms = receiver_perf_elapsed_ms(
            m_rx_frame_begin_ticks, NRF_RTC2->COUNTER);
        NRF_LOG_INFO("[ACK] END received: frame=%u checksum OK, sending ACK...",
                     (unsigned)packet[1]);
        receiver_send_image_ack(packet[1]);
        receiver_antenna_note_complete_image();
        if (receiver_forward_complete_image())
        {
            NRF_LOG_INFO("Legacy image queued for STM: id=%u bytes=%u",
                         packet[1], (unsigned)g_legacy_image_rx.image_length);
        }
        g_legacy_image_rx.active = false;
    }
}
