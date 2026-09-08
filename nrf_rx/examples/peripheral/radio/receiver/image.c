/**
 * @file image.c
 * @brief 专用接收板图像接收、重组与 STM32 转发实现
 *
 * 本文件从原 main.c 中剥离出所有与图像接收相关的代码：Radio 队列、
 * 协议解析、ACK 发送、STM32 帧封装等。
 */

#include "image.h"
#include "config.h"
#include "app_uart.h"
#include "nrf.h"
#include "nrf_delay.h"
#include "nrf_gpio.h"
#include "nrf_log.h"
#include "nrf_log_ctrl.h"
#include "rf1662.h"
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
    NRF_RADIO->FREQUENCY = 0u;
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
    NRF_LOG_INFO("Dedicated RX RADIO ready: 2400 MHz, 2 Mbit, legacy packet=%u bytes",
                 (unsigned)RADIO_PACKET_SIZE);
}

/** @brief Radio 收包中断：清 END、检查 CRC、入队、立即恢复接收。 */
void RADIO_IRQHandler(void)
{
    if (NRF_RADIO->EVENTS_END != 0u)
    {
        NRF_RADIO->EVENTS_END = 0u;
        NRF_RADIO->TASKS_RSSISTOP = 1u;

        if (m_antenna_scan_active)
        {
            ++m_antenna_scan_total;
            if (NRF_RADIO->CRCSTATUS != 0u)
            {
                ++m_antenna_scan_crc_ok;
                m_antenna_scan_rssi_sum += NRF_RADIO->RSSISAMPLE;
            }
            receiver_radio_arm();
            return;
        }

        if (NRF_RADIO->CRCSTATUS != 0u)
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

/** @brief 从环形队列取出一包并交给协议解析器。 */
bool receiver_forward_one(void)
{
    uint8_t head;
    if (m_receiver_queue.head == m_receiver_queue.tail)
    {
        return false;
    }
    head = m_receiver_queue.head;
    receiver_process_legacy_packet(m_receiver_queue.data[head]);
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

/** @brief 向发送端发送两次原始协议图片接收完成应答。 */
void receiver_send_image_ack(uint8_t image_id)
{
    uint8_t response[RADIO_PACKET_SIZE] __ALIGNED(4) = {0};
    uint8_t  repeat;
    uint32_t wait_count;

    response[0] = LEGACY_CMD_IMAGE_RECEIVED_RESPONSE;
    response[1] = image_id;
    memcpy(&response[2], g_legacy_image_rx.capsule_sn,
           LEGACY_CAPSULE_SN_SIZE);

    NRF_LOG_INFO("[ACK] start: frame=%u", (unsigned)image_id);  // ① 入口：函数被调用

    /* 短暂暂停接收，发送 ACK 后立即恢复 RX。 */
    NVIC_DisableIRQ(RADIO_IRQn);
    NRF_LOG_INFO("[ACK] IRQ disabled");                          // ② Radio 中断已关闭

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
    NRF_LOG_INFO("[ACK] radio disabled (wait=%u)",                // ③ Radio 已禁用
                 (unsigned)wait_count);

    nrf_gpio_pin_set(RECEIVER_MODE_PIN);
    NRF_LOG_INFO("[ACK] mode pin HIGH");                          // ④ 外部 RF 开关切到 TX

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
        NRF_LOG_INFO("[ACK] TXEN #%u done (wait=%u)",              // ⑤ 每次 TXEN 完成
                     (unsigned)repeat, (unsigned)wait_count);
    }

    nrf_gpio_pin_clear(RECEIVER_MODE_PIN);
    NRF_LOG_INFO("[ACK] mode pin LOW");                           // ⑥ 外部 RF 开关切回 RX

    NRF_RADIO->SHORTS = receiver_radio_rx_shorts();
    receiver_radio_arm();
    NVIC_ClearPendingIRQ(RADIO_IRQn);
    NVIC_EnableIRQ(RADIO_IRQn);
    NRF_LOG_INFO("[ACK] done: frame=%u", (unsigned)image_id);    // ⑦ 出口：ACK 流程完整结束
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
    memcpy(&g_stm_frame[LEGACY_STM_FRAME_HEADER_SIZE + 11u],
           g_legacy_image_rx.capsule_sn, LEGACY_CAPSULE_SN_SIZE);
    memcpy(&g_stm_frame[LEGACY_STM_FRAME_HEADER_SIZE +
                        LEGACY_DEVICE_INFO_SIZE],
           g_legacy_image_rx.image, g_legacy_image_rx.image_length);
    for (checksum_index = 0u;
         checksum_index < g_legacy_image_rx.image_length;
         checksum_index++)
    {
        checksum = (uint8_t)(checksum + g_legacy_image_rx.image[checksum_index]);
    }
    g_stm_frame[frame_length - 1u] = checksum;
    g_stm_frame_offset = 0u;
    g_stm_frame_length = frame_length;
    g_stm_image_length = g_legacy_image_rx.image_length;
    g_stm_image_id = g_legacy_image_rx.image_id;
    return true;
}

/** @brief 解析一个原始 TYGD31 Radio 包并推进图片重组状态机。 */
void receiver_process_legacy_packet(const uint8_t *packet)
{
    uint16_t packet_index;
    uint16_t copy_length;
    uint16_t offset;
    uint16_t index;
    uint8_t checksum = 0u;

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
        memset(&g_legacy_image_rx, 0, sizeof(g_legacy_image_rx));
        g_legacy_image_rx.active = true;
        g_legacy_image_rx.image_id = packet[1];
        memcpy(g_legacy_image_rx.capsule_sn, &packet[2],
               LEGACY_CAPSULE_SN_SIZE);
        g_legacy_image_rx.image_length = image_length;
        g_legacy_image_rx.packet_count = packet_count;
        g_legacy_image_rx.version_main = packet[14];              // 胶囊固件主版本号
        g_legacy_image_rx.version_sub  = packet[15];              // 胶囊固件子版本号
        g_legacy_image_rx.version_test = packet[16];              // 胶囊固件测试版本号
        NRF_LOG_INFO("Legacy image begin: id=%u length=%u packets=%u ver=%u.%u.%u",
                     packet[1], (unsigned)image_length, (unsigned)packet_count,
                     (unsigned)packet[14], (unsigned)packet[15], (unsigned)packet[16]);
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
            NRF_LOG_WARNING("Legacy image incomplete: id=%u received=%u/%u",
                            packet[1], (unsigned)g_legacy_image_rx.received_count,
                            (unsigned)g_legacy_image_rx.packet_count);
            return;
        }
        for (index = 0u; index < g_legacy_image_rx.image_length; index++)
        {
            checksum = (uint8_t)(checksum + g_legacy_image_rx.image[index]);
        }
        if (checksum != packet[10])
        {
            NRF_LOG_WARNING("Legacy image checksum failed: id=%u", packet[1]);
            g_legacy_image_rx.active = false;
            return;
        }
        NRF_LOG_INFO("[ACK] END received: frame=%u checksum OK, sending ACK...",
                     (unsigned)packet[1]);
        receiver_send_image_ack(packet[1]);
        if (receiver_forward_complete_image())
        {
            NRF_LOG_INFO("Legacy image queued for STM: id=%u bytes=%u",
                         packet[1], (unsigned)g_legacy_image_rx.image_length);
        }
        g_legacy_image_rx.active = false;
    }
}
