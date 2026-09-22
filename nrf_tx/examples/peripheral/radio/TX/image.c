/**
 * @file image.c
 * @brief 摄像头发送板图像采集、打包、无线发送与重发处理的实现
 *
 * 本文件从原 main.c 中剥离出所有与图像传输相关的代码，包括状态机、
 * Radio 收发底层、LED 控制与 1 ms 周期中断，便于阅读和独立维护。
 */

#include "image.h"
#include "capsule_sn_storage.h"
#include "config.h"
#include "dev_adxl362.h"
#include "ov7676.h"
#include "tx_runtime_log.h"
#include "nrf.h"
#include "nrf_gpio.h"
#include "nrf_log.h"
#include <string.h>

#if !(TX_LOG_ENABLED && TX_CONFIG_LOG_ENABLED)
#undef NRF_LOG_INFO
#undef NRF_LOG_WARNING
#undef NRF_LOG_ERROR
#undef NRF_LOG_HEXDUMP_INFO
#define NRF_LOG_INFO(...)
#define NRF_LOG_WARNING(...)
#define NRF_LOG_ERROR(...)
#define NRF_LOG_HEXDUMP_INFO(...)
#endif



/* ============================================================
 * 全局状态
 * ============================================================ */

/** 图像发送状态机全局实例。 */
image_tx_t g_image_tx;

/** 1 ms 系统时间戳。 */
volatile uint32_t g_time_ms;

/** 图像采集请求标志，由RTC2每个图片周期置位，主循环清除。 */
volatile bool g_capture_due;

/** 胶囊 8 字节序列号，从 FICR DEVICEID 读取。 */
uint8_t g_capsule_sn[LEGACY_CAPSULE_SN_SIZE];

/** Radio 接收环形队列描述符。 */
typedef struct
{
    uint8_t data[RADIO_QUEUE_DEPTH][RADIO_PACKET_SIZE]; /** 数据环形缓冲。 */
    volatile uint8_t head;                              /** 主循环读指针。 */
    volatile uint8_t tail;                              /** 中断写指针。 */
    volatile uint32_t dropped;                          /** 队列满丢包计数。 */
} radio_queue_t;

/** Radio 队列实例。 */
static radio_queue_t m_rx_queue;

/** Radio EasyDMA 接收缓冲区，按 4 字节对齐。 */
static uint8_t m_rx_packet[RADIO_PACKET_SIZE] __ALIGNED(4);

/** Radio EasyDMA 发送缓冲区，按 4 字节对齐。 */
static uint8_t m_tx_packet[RADIO_PACKET_SIZE] __ALIGNED(4);

/** 0x44只暂存新SN，收到0x46后才写入Flash。 */
static uint8_t m_pending_sn[LEGACY_CAPSULE_SN_SIZE];
static bool m_sn_update_pending;
static bool m_sn_config_window_active;
static bool m_sn_config_complete;
static uint32_t m_sn_config_last_activity_ms;

/** 图像采集准备状态：空闲或等待补光与自动算法稳定。 */
typedef enum
{
    CAPTURE_PREP_IDLE = 0,
    CAPTURE_PREP_WARMUP
} capture_prep_state_t;

static capture_prep_state_t m_capture_prep_state;
static uint32_t m_capture_warmup_deadline_ms;
static volatile bool m_sn_broadcast_due;
static bool m_fast_scan_active;
static uint32_t m_fast_scan_start_ms;
static uint32_t m_fast_scan_next_ms;
static uint8_t m_fast_scan_broadcast_count;

/* ============================================================
 * 序列号与初始化
 * ============================================================ */

/** @brief 初始化SN存储，并将当前生效SN同步到图像与Radio业务缓冲区。 */
void capsule_sn_init(void)
{
    const uint8_t *active_sn;

    capsule_sn_storage_init();                                                 // ① 根据Flash内容选择用户SN或FICR DEVICEID
    active_sn = capsule_sn_storage_get_active();
    memcpy(g_capsule_sn, active_sn, LEGACY_CAPSULE_SN_SIZE);                   // ② 保存当前生效SN供广播、图片包和ACK匹配使用
}

/** @brief 打开上电SN配置窗口并从当前时刻开始计算3秒无操作超时。 */
void capsule_sn_config_window_begin(void)
{
    m_sn_update_pending = false;
    m_sn_config_complete = false;
    m_sn_config_window_active = true;
    m_sn_config_last_activity_ms = g_time_ms;
    NRF_LOG_INFO("[SN CONFIG] window OPEN");
}

/** @brief 关闭SN配置窗口，同时丢弃尚未确认写入的暂存SN。 */
void capsule_sn_config_window_end(void)
{
    m_sn_config_window_active = false;
    m_sn_update_pending = false;
    NRF_LOG_INFO("[SN CONFIG] window CLOSED: result=%s",
                 m_sn_config_complete ? "configured" : "timeout");
}

/** @brief 查询本次上电配置流程是否已经通过0x46成功写入SN。 */
bool capsule_sn_config_is_complete(void)
{
    return m_sn_config_complete;
}

/** @brief 返回最近一条合法配置命令的时间，用于刷新3秒无操作超时。 */
uint32_t capsule_sn_config_last_activity_ms(void)
{
    return m_sn_config_last_activity_ms;
}

/** @brief 启动Radio所需的外部高频晶振HFXO（板上32 MHz晶体）。 */
void image_clock_init(void)
{
    NRF_CLOCK->EVENTS_HFCLKSTARTED = 0u;
    NRF_CLOCK->TASKS_HFCLKSTART = 1u;
    while (NRF_CLOCK->EVENTS_HFCLKSTARTED == 0u) {}
}



/* ============================================================
 * Radio 底层
 * ============================================================ */

/** @brief 安全关闭 Radio，等待进入 Disabled 状态。 */
static void radio_disable(void)
{
    if (NRF_RADIO->STATE != RADIO_STATE_STATE_Disabled)
    {
        NRF_RADIO->EVENTS_DISABLED = 0u;
        NRF_RADIO->TASKS_DISABLE = 1u;
        while (NRF_RADIO->EVENTS_DISABLED == 0u) {}
    }
}

/** @brief 设置接收 EasyDMA 缓冲并启动/继续接收。 */
static void radio_arm_rx(void)
{
    NRF_RADIO->PACKETPTR = (uint32_t)m_rx_packet;
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

/** @brief 配置摄像头板 Radio 收发链路（频率/速率/地址/CRC 与接收板一致）。 */
void radio_configure_image_link(void)
{
    NRF_RADIO->TXPOWER = RADIO_TXPOWER_TXPOWER_Pos4dBm;
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
    NRF_RADIO->SHORTS = RADIO_SHORTS_READY_START_Msk;
    NRF_RADIO->INTENSET = RADIO_INTENSET_END_Msk;
    NVIC_ClearPendingIRQ(RADIO_IRQn);
    NVIC_SetPriority(RADIO_IRQn, RADIO_IRQ_PRIORITY);
    NVIC_EnableIRQ(RADIO_IRQn);
    radio_arm_rx();
}

/** @brief 关闭Radio及其中断；下次发送或显式等待ACK时再启动。 */
void radio_enter_idle(void)
{
    NVIC_DisableIRQ(RADIO_IRQn);
    radio_disable();
    NRF_RADIO->EVENTS_END = 0u;
    NRF_RADIO->EVENTS_CRCOK = 0u;
    NRF_RADIO->EVENTS_CRCERROR = 0u;
    NVIC_ClearPendingIRQ(RADIO_IRQn);
}

/** @brief 阻塞发送一个254字节Radio包；仅按调用者要求恢复RX。 */
static void radio_send_packet(const uint8_t *packet, bool resume_rx)
{
    NVIC_DisableIRQ(RADIO_IRQn);
    radio_disable();
    NRF_RADIO->PACKETPTR = (uint32_t)packet;
    NRF_RADIO->SHORTS = RADIO_SHORTS_READY_START_Msk |
                        RADIO_SHORTS_END_DISABLE_Msk;
    NRF_RADIO->EVENTS_END = 0u;
    NRF_RADIO->EVENTS_DISABLED = 0u;
    NRF_RADIO->TASKS_TXEN = 1u;
    while (NRF_RADIO->EVENTS_DISABLED == 0u) {}
    NRF_RADIO->EVENTS_END = 0u;
    NVIC_ClearPendingIRQ(RADIO_IRQn);
    if (resume_rx)
    {
        NRF_RADIO->SHORTS = RADIO_SHORTS_READY_START_Msk;
        radio_arm_rx();
        NVIC_EnableIRQ(RADIO_IRQn);
    }
}

/** @brief 计算ZAYS控制帧数据区的8位累加校验和。 */
static uint8_t control_checksum(const uint8_t *data, uint16_t length)
{
    uint8_t checksum = 0u;
    uint16_t index;
    for (index = 0u; index < length; ++index)
    {
        checksum = (uint8_t)(checksum + data[index]);
    }
    return checksum;
}

/** @brief 组装配置应答并通过Radio重复发送3次，提高短帧可靠性。 */
static void radio_send_control_response(uint8_t command,
                                        const uint8_t *payload,
                                        uint16_t payload_length)
{
    uint8_t repeat;
    uint16_t frame_length = (uint16_t)(payload_length + 8u);
    memset(m_tx_packet, 0, sizeof(m_tx_packet));
    m_tx_packet[0] = 0x5Au;
    m_tx_packet[1] = 0x41u;
    m_tx_packet[2] = 0x59u;
    m_tx_packet[3] = 0x53u;
    m_tx_packet[4] = command;
    m_tx_packet[5] = (uint8_t)(payload_length >> 8);
    m_tx_packet[6] = (uint8_t)payload_length;
    if ((payload != NULL) && (payload_length != 0u))
    {
        memcpy(&m_tx_packet[7], payload, payload_length);
    }
    m_tx_packet[frame_length - 1u] = control_checksum(payload, payload_length);
    NRF_LOG_INFO("[CTRL] RADIO TX response: cmd=0x%02x data_len=%u frame_len=%u repeat=3",
                 (unsigned)command,
                 (unsigned)payload_length,
                 (unsigned)frame_length);
    NRF_LOG_HEXDUMP_INFO(m_tx_packet, frame_length);
    for (repeat = 0u; repeat < 3u; ++repeat)
    {
        radio_send_packet(m_tx_packet, true); /* 配置窗口内继续接收下一条命令。 */
    }
}

/**
 * @brief 解析并执行0x40～0x46出厂SN配置命令。
 * @return true表示输入是ZAYS控制帧并已处理，false表示不是控制帧。
 */
static bool radio_process_control_frame(const uint8_t *packet)
{
    uint16_t payload_length;
    uint16_t frame_length;
    capsule_sn_t requested_sn;

    /* 只处理以“ZAYS”开头的控制帧，其他Radio包交还调用者继续识别。 */
    if ((packet[0] != 0x5Au) || (packet[1] != 0x41u) ||
        (packet[2] != 0x59u) || (packet[3] != 0x53u))
    {
        return false;
    }

    /* 完整帧长度 = 7字节头 + payload + 1字节校验和。 */
    payload_length = LegacyProtocol_GetU16Be(&packet[5]);
    frame_length = (uint16_t)(payload_length + 8u);
    NRF_LOG_INFO("[CTRL] RADIO RX request: cmd=0x%02x data_len=%u frame_len=%u",
                 (unsigned)packet[4],
                 (unsigned)payload_length,
                 (unsigned)frame_length);
    NRF_LOG_HEXDUMP_INFO(packet,
                         (frame_length <= LEGACY_CONTROL_FRAME_MAX_SIZE) ?
                         frame_length : LEGACY_CONTROL_FRAME_MAX_SIZE);

    /* 长度越界或校验和错误时，消费该控制帧但不执行命令。 */
    if ((frame_length > LEGACY_CONTROL_FRAME_MAX_SIZE) ||
        (control_checksum(&packet[7], payload_length) !=
         packet[frame_length - 1u]))
    {
        NRF_LOG_WARNING("[CTRL] invalid frame: cmd=0x%02x len=%u",
                        (unsigned)packet[4],
                        (unsigned)payload_length);
        return true;
    }

    /* SN配置窗口关闭后拒绝新配置；已成功的重复0x46只重发0x47，避免重复写Flash。 */
    if (((packet[4] == LEGACY_CMD_SN_PREPARE_REQUEST) ||
         (packet[4] == LEGACY_CMD_DEVICE_ID_QUERY_REQUEST) ||
         (packet[4] == LEGACY_CMD_SN_SET_REQUEST) ||
         (packet[4] == LEGACY_CMD_SN_CONFIRM_REQUEST)) &&
        !m_sn_config_window_active)
    {
        if ((packet[4] == LEGACY_CMD_SN_CONFIRM_REQUEST) &&
            m_sn_config_complete)
        {
            NRF_LOG_INFO("[SN CONFIG] duplicate confirm; resend 0x47");
            radio_send_control_response(LEGACY_CMD_SN_CONFIRM_RESPONSE,
                                        NULL, 0u);
        }
        else
        {
            NRF_LOG_WARNING("[SN CONFIG] reject cmd=0x%02x: window closed",
                            (unsigned)packet[4]);
            radio_send_control_response(LEGACY_CMD_CONTROL_ERROR_RESPONSE,
                                        NULL, 0u);
        }
        return true;
    }

    /* 合法命令会刷新配置窗口活动时间，防止一组配置命令处理中途超时。 */
    if (((packet[4] == LEGACY_CMD_SN_PREPARE_REQUEST) &&
         (payload_length == 0u)) ||
        ((packet[4] == LEGACY_CMD_DEVICE_ID_QUERY_REQUEST) &&
         (payload_length == 0u)) ||
        ((packet[4] == LEGACY_CMD_SN_SET_REQUEST) &&
         (payload_length == 16u)) ||
        ((packet[4] == LEGACY_CMD_SN_CONFIRM_REQUEST) &&
         (payload_length == 0u)))
    {
        m_sn_config_last_activity_ms = g_time_ms;
        NRF_LOG_INFO("[SN CONFIG] valid cmd=0x%02x; inactivity timer reset",
                     (unsigned)packet[4]);
    }

    /* 0x40：开始一次新的SN配置，清除之前未确认的暂存SN。 */
    if ((packet[4] == LEGACY_CMD_SN_PREPARE_REQUEST) &&
        (payload_length == 0u))
    {
        m_sn_update_pending = false;
        NRF_LOG_INFO("[CTRL] factory SN prepare accepted; pending cleared");
        radio_send_control_response(LEGACY_CMD_SN_PREPARE_RESPONSE,
                                    NULL, 0u);
    }
    /* 0x42：读取并返回TX芯片的8字节DEVICEID。 */
    else if ((packet[4] == LEGACY_CMD_DEVICE_ID_QUERY_REQUEST) &&
             (payload_length == 0u))
    {
        NRF_LOG_INFO("[CTRL] DEVICEID query");
        NRF_LOG_HEXDUMP_INFO((const uint8_t *)NRF_FICR->DEVICEID, 8u);
        radio_send_control_response(LEGACY_CMD_DEVICE_ID_QUERY_RESPONSE,
                                    (const uint8_t *)NRF_FICR->DEVICEID, 8u);
    }
    /* 0x44：校验目标DEVICEID，匹配后只把新SN暂存在RAM中。 */
    else if ((packet[4] == LEGACY_CMD_SN_SET_REQUEST) &&
             (payload_length == 16u))
    {
        NRF_LOG_INFO("[CTRL] factory SN set request");
        NRF_LOG_INFO("[CTRL] request DEVICEID / local DEVICEID:");
        NRF_LOG_HEXDUMP_INFO(&packet[7], 8u);
        NRF_LOG_HEXDUMP_INFO((const uint8_t *)NRF_FICR->DEVICEID, 8u);
        if (memcmp(&packet[7], (const void *)NRF_FICR->DEVICEID, 8u) != 0)
        {
            NRF_LOG_WARNING("[CTRL] SN set rejected: DEVICEID mismatch");
            radio_send_control_response(LEGACY_CMD_CONTROL_ERROR_RESPONSE,
                                        NULL, 0u);
        }
        else
        {
            memcpy(m_pending_sn, &packet[15], LEGACY_CAPSULE_SN_SIZE);
            m_sn_update_pending = true;
            NRF_LOG_INFO("[CTRL] SN staged; waiting for cmd=0x46");
            NRF_LOG_HEXDUMP_INFO(m_pending_sn, LEGACY_CAPSULE_SN_SIZE);
            radio_send_control_response(LEGACY_CMD_SN_SET_RESPONSE,
                                        m_pending_sn,
                                        LEGACY_CAPSULE_SN_SIZE);
        }
    }
    /* 0x46：把暂存SN写入Flash，回读生效后返回0x47。 */
    else if ((packet[4] == LEGACY_CMD_SN_CONFIRM_REQUEST) &&
             (payload_length == 0u))
    {
        NRF_LOG_INFO("[CTRL] factory SN confirm: pending=%u",
                     (unsigned)m_sn_update_pending);
        if (m_sn_update_pending)
        {
            memcpy(requested_sn.bytes, m_pending_sn,
                   LEGACY_CAPSULE_SN_SIZE);
            if (capsule_sn_storage_write(&requested_sn))
            {
                capsule_sn_init();
                m_sn_update_pending = false;
                m_sn_config_complete = true;
                NRF_LOG_INFO("[CTRL] SN Flash write OK; active SN:");
                NRF_LOG_HEXDUMP_INFO(g_capsule_sn,
                                     LEGACY_CAPSULE_SN_SIZE);
                radio_send_control_response(LEGACY_CMD_SN_CONFIRM_RESPONSE,
                                            NULL, 0u);
            }
            else
            {
                NRF_LOG_ERROR("[CTRL] SN Flash write FAILED");
                radio_send_control_response(LEGACY_CMD_CONTROL_ERROR_RESPONSE,
                                            NULL, 0u);
            }
        }
        else
        {
            NRF_LOG_WARNING("[CTRL] confirm rejected: no staged SN");
            radio_send_control_response(LEGACY_CMD_CONTROL_ERROR_RESPONSE,
                                        NULL, 0u);
        }
    }
    /* 未定义命令或payload长度不符合协议时统一返回控制错误。 */
    else
    {
        NRF_LOG_WARNING("[CTRL] unsupported command/length: cmd=0x%02x len=%u",
                        (unsigned)packet[4],
                        (unsigned)payload_length);
        radio_send_control_response(LEGACY_CMD_CONTROL_ERROR_RESPONSE,
                                    NULL, 0u);
    }
    return true;
}

/** @brief 主循环服务：RTC2置位广播请求后，在图像链路空闲时广播当前生效SN。 */
void capsule_sn_broadcast_service(void)
{
    if (m_fast_scan_active || g_image_tx.active || g_image_tx.awaiting_ack ||
        !m_sn_broadcast_due)
    {
        return;
    }
    m_sn_broadcast_due = false;
    memset(m_tx_packet, 0, sizeof(m_tx_packet));
    m_tx_packet[0] = LEGACY_CMD_CAPSULE_SN_BROADCAST;
    memcpy(&m_tx_packet[1], g_capsule_sn, LEGACY_CAPSULE_SN_SIZE);
    tx_runtime_log_sn_broadcast(g_capsule_sn);
    radio_send_packet(m_tx_packet, false);
}

/** @brief 仅在RX请求后限时密集广播SN；超时自动恢复原图像周期。 */
void image_fast_scan_service(void)
{
    uint32_t elapsed;                                           // 本次快速扫描已经运行的时间

    /* 未收到RX的快速扫描请求时不执行密集SN广播。 */
    if (!m_fast_scan_active)
    {
        return;
    }

    elapsed = (uint32_t)(g_time_ms - m_fast_scan_start_ms);

    /* 扫描窗口到期后退出扫描模式，等待下一个RTC2周期重新采集图片。 */
    if (elapsed >= FAST_SCAN_DURATION_MS)
    {
        m_fast_scan_active = false;                             // 结束快速扫描状态
        g_capture_due = false;                                  // 丢弃扫描期间积累的图片采集请求
        m_sn_broadcast_due = false;                             // 丢弃扫描期间积累的普通SN广播请求
        NRF_LOG_INFO("[SCAN] window complete; resume image schedule");
        return;
    }

    /* 未到下一个8 ms广播时刻时立即返回，避免主循环连续无间隔发送。 */
    if ((int32_t)(g_time_ms - m_fast_scan_next_ms) < 0)
    {
        return;
    }

    /* 前120 ms内每4次SN广播插入一次0x11，帮助RX快速确认扫描已经开始。 */
    if ((elapsed < 120u) && ((m_fast_scan_broadcast_count % 4u) == 0u))
    {
        memset(m_tx_packet, 0, sizeof(m_tx_packet));
        m_tx_packet[0] = LEGACY_CMD_FAST_SCAN_START;            // 0x11：快速扫描开始同步包
        memcpy(&m_tx_packet[1], g_capsule_sn, LEGACY_CAPSULE_SN_SIZE);
        radio_send_packet(m_tx_packet, false);
    }

    /* 每个广播周期都发送0x05和当前8字节胶囊SN，供RX各天线采集RSSI。 */
    memset(m_tx_packet, 0, sizeof(m_tx_packet));
    m_tx_packet[0] = LEGACY_CMD_CAPSULE_SN_BROADCAST;           // 0x05：胶囊SN广播包
    memcpy(&m_tx_packet[1], g_capsule_sn, LEGACY_CAPSULE_SN_SIZE);
    radio_send_packet(m_tx_packet, false);
    ++m_fast_scan_broadcast_count;                              // 记录本次扫描已经发送的SN广播数量
    m_fast_scan_next_ms = g_time_ms + FAST_SCAN_SN_PERIOD_MS;   // 安排下一次密集广播时间，当前配置为8 ms后
}

/** @brief 配置成功后立即连续广播指定次数的当前SN。 */
void capsule_sn_broadcast_burst(uint8_t repeat_count)
{
    uint8_t index;
    memset(m_tx_packet, 0, sizeof(m_tx_packet));
    m_tx_packet[0] = LEGACY_CMD_CAPSULE_SN_BROADCAST;
    memcpy(&m_tx_packet[1], g_capsule_sn, LEGACY_CAPSULE_SN_SIZE);
    tx_runtime_log_sn_broadcast(g_capsule_sn);
    for (index = 0u; index < repeat_count; ++index)
    {
        radio_send_packet(m_tx_packet, false);
    }
}



/* ============================================================
 * LED 控制
 * ============================================================ */

/** @brief 初始化 P0.08 拍照补光灯，默认低电平（熄灭）。 */
void image_tx_led_init(void)
{
    nrf_gpio_cfg_output(IMAGE_TX_LED_PIN);
    nrf_gpio_pin_clear(IMAGE_TX_LED_PIN);
}

/** @brief 控制 P0.08 拍照补光灯亮灭。 */
void image_tx_led_set(bool on)
{
    if (on)
    {
        nrf_gpio_pin_set(IMAGE_TX_LED_PIN);
    }
    else
    {
        nrf_gpio_pin_clear(IMAGE_TX_LED_PIN);
    }
}



/* ============================================================
 * Radio 队列与中断
 * ============================================================ */

/** @brief Radio 接收中断：CRC OK 数据入队，丢包计数，立即恢复接收。 */
void RADIO_IRQHandler(void)
{
    if (NRF_RADIO->EVENTS_END != 0u)
    {
        NRF_RADIO->EVENTS_END = 0u;
        if (NRF_RADIO->CRCSTATUS != 0u)
        {
            uint8_t tail = m_rx_queue.tail;
            uint8_t next = (uint8_t)((tail + 1u) % RADIO_QUEUE_DEPTH);
            if (next != m_rx_queue.head)
            {
                memcpy(m_rx_queue.data[tail], m_rx_packet, RADIO_PACKET_SIZE);
                m_rx_queue.tail = next;
            }
            else
            {
                ++m_rx_queue.dropped;
            }
        }
        radio_arm_rx();
    }
}

/** @brief 主循环中轮询处理收到的 Radio 控制包（当前实现为 ACK 检测）。 */
void radio_rx_process(void)
{
    if (m_rx_queue.head != m_rx_queue.tail)                                    // ① 队列非空：取出一包处理
    {
        uint8_t head = m_rx_queue.head;                                       //   当前读指针
        const uint8_t *packet = m_rx_queue.data[head];                        //   当前 Radio 包内容
        if (m_sn_config_window_active)
        {
            NRF_LOG_INFO("[SN CONFIG] RF packet received: %02x %02x %02x %02x",
                         (unsigned)packet[0],
                         (unsigned)packet[1],
                         (unsigned)packet[2],
                         (unsigned)packet[3]);
        }
        if (radio_process_control_frame(packet))
        {
            /* ZAYS control frame handled above. */
        }
        else if (g_image_tx.awaiting_ack &&
            ((packet[0] == LEGACY_CMD_IMAGE_RECEIVED_RESPONSE) ||
             (packet[0] == LEGACY_CMD_FAST_SCAN_REQUEST)) &&
            (packet[1] == (uint8_t)g_image_tx.frame_id) &&                   // ③ 帧 ID 必须一致（防止旧 ACK 误清当前等待）
            (memcmp(&packet[2], g_capsule_sn, LEGACY_CAPSULE_SN_SIZE) == 0))  // ④ 序列号必须一致（防止其他设备的 ACK 干扰）
        {
            bool fast_scan_requested =
                (packet[0] == LEGACY_CMD_FAST_SCAN_REQUEST);
            if (!fast_scan_requested)
            {
                tx_runtime_log_ack_received(g_image_tx.frame_id);
            }
            g_image_tx.awaiting_ack = false;                                  // ⑤ ACK 匹配成功：清等待标志
            g_image_tx.retry_count = 0u;                                      //   清重发计数
            radio_enter_idle();                                               //   ACK已收到，立即关闭持续RX
            cx93510_host_suspend();                                           //   ACK后关闭nRF侧SPIM0
            if (fast_scan_requested)
            {
                m_fast_scan_active = true;
                m_fast_scan_start_ms = g_time_ms;
                m_fast_scan_next_ms = g_time_ms;
                m_fast_scan_broadcast_count = 0u;
                g_capture_due = false;
                m_sn_broadcast_due = false;
                NRF_LOG_INFO("[SCAN] RX request accepted; pause image for %ums",
                             (unsigned)FAST_SCAN_DURATION_MS);
            }
        }
        m_rx_queue.head = (uint8_t)((head + 1u) % RADIO_QUEUE_DEPTH);         // ⑥ 无论是否匹配 ACK，都要把这包出队，否则会卡队列
    }
}



/* ============================================================
 * TIMER1 1 ms 周期中断
 * ============================================================ */

/** @brief TIMER1 1 ms周期中断：仅在拍照、发送和ACK等待期间运行。 */
void TIMER1_IRQHandler(void)
{
    if (NRF_TIMER1->EVENTS_COMPARE[0] != 0u)
    {
        NRF_TIMER1->EVENTS_COMPARE[0] = 0u;
        ++g_time_ms;
    }
}

/** @brief RTC2周期中断只置请求标志，硬件访问仍在主循环执行。 */
void RTC2_IRQHandler(void)
{
    if (NRF_RTC2->EVENTS_COMPARE[0] != 0u)
    {
        NRF_RTC2->EVENTS_COMPARE[0] = 0u;
        NRF_RTC2->CC[0] = (NRF_RTC2->COUNTER +
                           ((32768u * IMAGE_PERIOD_MS) / 1000u)) & 0x00FFFFFFu;
        m_sn_broadcast_due = true;
#if IMAGE_TRANSMISSION_ENABLED
        g_capture_due = true;
#endif
    }
}

/**
 * @brief  图像低功耗调度器初始化
 * @note   使用 nRF52 的 RTC2 + RC 低频时钟，实现周期性唤醒，
 *         每隔 IMAGE_PERIOD_MS 毫秒触发一次 RTC2_IRQHandler 中断。
 */
void image_low_power_scheduler_init(void)
{
    /* ------------------------------------------------------------------
     * 1. 配置低频时钟源（LFCLK）为内部 RC 振荡器
     *    - LFCLK = 32768 Hz，是 RTC 的时钟源
     *    - RC 振荡器：功耗最低、启动最快，但精度较差（±250 ppm）
     *    - 若选 XTAL 则需要外部 32.768kHz 晶振，精度高但功耗和成本更高
     * ------------------------------------------------------------------ */
    NRF_CLOCK->LFCLKSRC = CLOCK_LFCLKSRC_SRC_RC;

    /* 清除 LFCLKSTARTED 事件标志，避免旧的标志误判 */
    NRF_CLOCK->EVENTS_LFCLKSTARTED = 0u;

    /* 触发 LFCLK 启动任务 */
    NRF_CLOCK->TASKS_LFCLKSTART = 1u;

    /* 轮询等待 LFCLK 启动完成
     * 必须等 LFCLK 稳定后才能配置 RTC，否则 RTC 无法正常工作 */
    while (NRF_CLOCK->EVENTS_LFCLKSTARTED == 0u) {}

    /* ------------------------------------------------------------------
     * 2. 停止并清零 RTC2
     *    - 配置前先 STOP，防止计数器在配置过程中继续跑
     *    - CLEAR 让计数器从 0 开始，保证定时周期准确
     * ------------------------------------------------------------------ */
    NRF_RTC2->TASKS_STOP  = 1u;
    NRF_RTC2->TASKS_CLEAR = 1u;

    /* ------------------------------------------------------------------
     * 3. 设置预分频器
     *    - RTC 计数频率 = 32768 / 2^PRESCALER
     *    - PRESCALER = 0 → 32768 Hz，每 tick ≈ 30.5 µs，分辨率最高
     *    - 适合毫秒级定时；若要秒级定时可增大 PRESCALER
     * ------------------------------------------------------------------ */
    NRF_RTC2->PRESCALER = 0u;

    /* ------------------------------------------------------------------
     * 4. 设置比较值 CC[0]，决定定时周期
     *    - 计数值从 0 开始，每 tick +1
     *    - 当计数值 == CC[0] 时，触发 EVENTS_COMPARE[0]
     *    - 计数频率 32768 Hz，所以：
     *          CC[0] = 32768 * IMAGE_PERIOD_MS / 1000
     *    - 例如 IMAGE_PERIOD_MS = 100 → CC[0] = 3276 → 100 ms 触发一次
     *    - 注意：CC[0] 是 24 位寄存器，最大 16777215，约 512 秒
     * ------------------------------------------------------------------ */
    NRF_RTC2->CC[0] = (32768u * IMAGE_PERIOD_MS) / 1000u;

    /* 清除 COMPARE[0] 事件标志，防止残留标志导致中断误触发 */
    NRF_RTC2->EVENTS_COMPARE[0] = 0u;

    /* ------------------------------------------------------------------
     * 5. 使能 COMPARE[0] 中断
     *    - nRF52 外设是"事件 → 中断"两级结构：
     *          计数匹配 → EVENTS_COMPARE[0] = 1
     *                    → 若 INTENSET 对应位为 1
     *                    → 触发 RTC2_IRQn
     *    - 中断服务函数中必须手动清 EVENTS_COMPARE[0]，否则会反复触发
     * ------------------------------------------------------------------ */
    NRF_RTC2->INTENSET = RTC_INTENSET_COMPARE0_Msk;

    /* ------------------------------------------------------------------
     * 6. 配置 NVIC（嵌套向量中断控制器）
     * ------------------------------------------------------------------ */

    /* 清除之前可能挂起的 RTC2 中断，避免一使能就立即进中断 */
    NVIC_ClearPendingIRQ(RTC2_IRQn);

    /* 设置中断优先级
     * nRF52 优先级范围：0（最高）~ 7（最低）
     * 这里设为 7（最低），说明该定时任务不紧急，可被其他中断打断 */
    NVIC_SetPriority(RTC2_IRQn, 7u);

    /* 使能 RTC2 中断 */
    NVIC_EnableIRQ(RTC2_IRQn);

    /* ------------------------------------------------------------------
     * 7. 启动 RTC2
     *    计数器开始运行，定时器正式工作
     *    此后每 IMAGE_PERIOD_MS 毫秒触发一次 RTC2_IRQHandler
     * ------------------------------------------------------------------ */
    NRF_RTC2->TASKS_START = 1u;
}

bool image_runtime_busy(void)
{
    return m_fast_scan_active ||                                // RX已请求天线扫描，TX正在快速广播SN
           m_sn_broadcast_due ||                                // RTC2已产生普通SN广播请求，等待主循环发送
           g_capture_due ||                                     // RTC2已产生图片采集请求，等待启动摄像头
           (m_capture_prep_state != CAPTURE_PREP_IDLE) ||       // 摄像头、补光灯和加速度计正在预热，等待正式采集
           g_image_tx.active ||                                 // 当前图片仍有BEGIN、DATA或END包需要发送
           g_image_tx.awaiting_ack;                             // END已发送，Radio正在等待RX返回图片ACK或扫描请求
}



/* ============================================================
 * 图像发送状态机
 * ============================================================ */

/** @brief 选择下一段待发送数据：JPEG 配置块或 JPEG 图像块，并计算总分片数。 */
static void select_image_block(bool config_block)
{
    g_image_tx.config_block = config_block;
    g_image_tx.fragment_index = 0u;
    g_image_tx.block_pass_index = 0u;
    g_image_tx.block_sent = 0u;
    if (config_block)
    {
        g_image_tx.block_offset = g_image_tx.frame.config_offset;
        g_image_tx.block_size = g_image_tx.frame.config_size;
    }
    else
    {
        g_image_tx.block_offset = g_image_tx.frame.jpeg_offset;
        g_image_tx.block_size = g_image_tx.frame.jpeg_size;
    }
    g_image_tx.fragment_count = (uint16_t)((g_image_tx.block_size +
                                            IMAGE_PAYLOAD_SIZE - 1u) /
                                           IMAGE_PAYLOAD_SIZE);
}

/**
 * @brief 非阻塞处理周期采集：开灯并唤醒，等待AE/AWB稳定后抓图，再休眠。
 * @note  补光预热期间函数立即返回，主循环仍可处理Radio、日志和看门狗。
 */
void image_capture_task(void)
{
    bool capture_ok;
    adxl362_sample_t accel_sample;

    if (m_fast_scan_active)
    {
        g_capture_due = false;
        return;
    }

    if (m_capture_prep_state == CAPTURE_PREP_WARMUP)                            // ① 已开灯并唤醒：等待稳定截止时间
    {
        if ((int32_t)(g_time_ms - m_capture_warmup_deadline_ms) < 0)
        {
            return;
        }

        g_image_tx.accel_valid = adxl362_read_sample(&accel_sample);            // ② 在触发图像采集前锁存本帧三轴原始值
        (void)adxl362_standby();                                                // ③ 读完立即待机，直至下一拍摄周期
        if (g_image_tx.accel_valid)
        {
            g_image_tx.accel_x_raw = accel_sample.raw_x;
            g_image_tx.accel_y_raw = accel_sample.raw_y;
            g_image_tx.accel_z_raw = accel_sample.raw_z;
        }
        capture_ok = cx93510_capture_one(&g_image_tx.frame,                     // ④ 紧接着触发采集，使姿态与本帧曝光时刻对应
                                         IMAGE_CAPTURE_TIMEOUT_MS);
        image_tx_led_set(false);                                                // ④ 图像已进入CX93510帧缓冲，立即关闭补光灯
        (void)ov7676_sleep();                                                   // ⑤ 传感器进入软件休眠，发送阶段不再持续工作
        m_capture_prep_state = CAPTURE_PREP_IDLE;                               // 采集准备状态恢复为空闲。
        if (!capture_ok)
        {
            cx93510_host_suspend();
            return;
        }
    }
    else
    {
        if (!g_capture_due)                                                      // ⑤ 周期未到：保持传感器休眠
        {
            return;
        }
        g_capture_due = false;                                                   // ⑥ 消费本次周期请求
        if (g_image_tx.active || g_image_tx.awaiting_ack)                       // ⑦ 上一帧未结束：跳过本周期，避免覆盖帧缓冲
        {
            return;
        }

        cx93510_host_resume();                                                   // ⑧ 拍照前恢复nRF侧SPIM0，P0.11始终为高
        image_tx_led_set(true);                                                  // ⑧ 先开补光灯，使传感器从唤醒起就看到正式光源
        if (!adxl362_measurement_start())                                        // ⑨ 同期启动100Hz测量，预热25ms后可取得新样本
        {
            g_image_tx.accel_valid = false;
        }
        if (!ov7676_wakeup())                                                     // ⑨ 恢复OV7676连续视频输出
        {
            (void)adxl362_standby();      	// 加速度计重新待机。
            image_tx_led_set(false);       	// 关闭补光灯
            (void)ov7676_sleep();           // 摄像头重新休眠
            cx93510_host_suspend();      	// 关闭CX93510主机SPI
            return;                     	// 放弃本次采集
        }
        m_capture_warmup_deadline_ms = g_time_ms +
                                       IMAGE_CAPTURE_LIGHT_WARMUP_MS;            // ⑩ 非阻塞等待约1帧，沿用旧工程稳定时序
        m_capture_prep_state = CAPTURE_PREP_WARMUP;
        return;
    }
    if (g_image_tx.frame.jpeg_size == 0u)                                      // ⑥ 空帧不分配帧号，也不启动Radio
    {
        image_tx_led_set(false);
        cx93510_host_suspend();
        return;
    }
    if (g_image_tx.frame.jpeg_size > LEGACY_IMAGE_MAX_SIZE)                    // ⑦ 在TX端拒绝STM32旧记录必然容纳不下的图像
    {
        tx_runtime_log_image_rejected(g_image_tx.frame.jpeg_size,
                                      LEGACY_IMAGE_MAX_SIZE);
        image_tx_led_set(false);
        cx93510_host_suspend();
        return;
    }
    g_image_tx.frame_id = (uint16_t)((g_image_tx.frame_id + 1u) % 255u);        // ⑧ 仅合法帧ID自增，供接收端区分不同帧
    if (g_image_tx.accel_valid)
    {
        adxl362_log_raw_sample(g_image_tx.frame_id, &accel_sample);
    }
    g_image_tx.legacy_checksum = 0u;                                            // ⑧ 清零本帧 JPEG 8 位累加校验和
    g_image_tx.retry_count = 0u;                                                // ⑨ 清零本帧已重发计数
    g_image_tx.legacy_send_begin = true;                                        // ⑩ 置首轮标志，让 image_tx_service 在第 0 片连发两次 BEGIN 包
    select_image_block(false);                                                  // ⑪ 选中 JPEG 块（不是 config 块），写入 offset/size 并算出总分片数
    tx_runtime_log_image_broadcast(g_image_tx.frame_id,
                                   g_image_tx.frame.jpeg_size,
                                   g_image_tx.fragment_count);
    g_image_tx.next_fragment_ms = g_time_ms;                                    // ⑫ 允许下一分片立即发送（image_tx_service 不会等到未来时刻）
    g_image_tx.active = true;                                                   // ⑬ 置位活动标志，通知 image_tx_service 开始处理本帧
}

/** @brief 非阻塞式图像分片发送状态机，每次最多发送一个分片。 */
void image_tx_service(void)
{
    uint16_t remaining;                                                          // 本次要发送的剩余字节数
    uint16_t checksum_index;                                                     // 累加校验和用的循环变量
    uint8_t payload_length;                                                      // 本次实际载荷长度（最后一包可能小于 IMAGE_PAYLOAD_SIZE）
    if (!g_image_tx.active ||                                                    // ① 没有待发送帧：直接返回
        ((int32_t)(g_time_ms - g_image_tx.next_fragment_ms) < 0))                // ② 还没到下一片允许发送的时间：返回（节流 1 ms）
    {
        return;
    }
    remaining = (uint16_t)(g_image_tx.block_size - g_image_tx.block_sent);       // ③ 计算本块还剩多少字节未发
    payload_length = (remaining > IMAGE_PAYLOAD_SIZE) ?                          // ④ 本次载荷：未发完取满包，否则取剩余字节
                     (uint8_t)IMAGE_PAYLOAD_SIZE : (uint8_t)remaining;
    if ((g_image_tx.fragment_index == 0u) &&                                     // ⑤ 本块第 0 片 且 第 0 遍 且 首轮标志：连发 2 次 BEGIN 包
        (g_image_tx.block_pass_index == 0u) &&
        g_image_tx.legacy_send_begin)
    {
        memset(m_tx_packet, 0, sizeof(m_tx_packet));                             //   清空发送 buffer
        m_tx_packet[0] = LEGACY_CMD_IMAGE_BEGIN;                                 //   命令字：图像开始包
        m_tx_packet[1] = (uint8_t)g_image_tx.frame_id;                           //   帧 ID
        memcpy(&m_tx_packet[2], g_capsule_sn, LEGACY_CAPSULE_SN_SIZE);           //   8 字节胶囊序列号
        LegacyProtocol_PutU16Be(&m_tx_packet[10], g_image_tx.block_size);        //   本块总字节数（大端）
        LegacyProtocol_PutU16Be(&m_tx_packet[12], g_image_tx.fragment_count);    //   本块总分片数（大端）
        m_tx_packet[14] = VERSION_MAIN;                                          //   胶囊固件主版本号
        m_tx_packet[15] = VERSION_SUB;                                           //   胶囊固件子版本号
        m_tx_packet[16] = VERSION_TEST;                                          //   胶囊固件测试版本号
        m_tx_packet[LEGACY_BEGIN_ACCEL_VALID_OFFSET] = g_image_tx.accel_valid ? 1u : 0u;
        LegacyProtocol_PutI16Be(&m_tx_packet[LEGACY_BEGIN_ACCEL_X_OFFSET], g_image_tx.accel_x_raw);
        LegacyProtocol_PutI16Be(&m_tx_packet[LEGACY_BEGIN_ACCEL_Y_OFFSET], g_image_tx.accel_y_raw);
        LegacyProtocol_PutI16Be(&m_tx_packet[LEGACY_BEGIN_ACCEL_Z_OFFSET], g_image_tx.accel_z_raw);
        radio_send_packet(m_tx_packet, false);                                   //   第 1 次发 BEGIN
        radio_send_packet(m_tx_packet, false);                                   //   第 2 次发 BEGIN（抗丢包）
    }

    memset(m_tx_packet, 0, sizeof(m_tx_packet));                              // ⑥ 准备 DATA 包：先清零 buffer
    m_tx_packet[0] = LEGACY_CMD_IMAGE_DATA;                                   //   命令字：图像数据包
    m_tx_packet[1] = (uint8_t)g_image_tx.frame_id;                            //   帧 ID
    memcpy(&m_tx_packet[2], g_capsule_sn, LEGACY_CAPSULE_SN_SIZE);            //   8 字节胶囊序列号
    LegacyProtocol_PutU16Be(&m_tx_packet[10], g_image_tx.fragment_index);     //   当前分片编号（大端）
    if (!cx93510_frame_buffer_read(g_image_tx.block_offset +                  // ⑦ 从 CX93510 帧缓冲读一段 JPEG 到发送 buffer 载荷区
                                   g_image_tx.block_sent,
                                   &m_tx_packet[LEGACY_IMAGE_PACKET_HEADER_SIZE],
                                   payload_length))
    {
        g_image_tx.active = false;
        g_image_tx.awaiting_ack = false;
        g_image_tx.retry_count = 0u;
        image_tx_led_set(false);
        cx93510_host_suspend();
        return;
    }
    for (checksum_index = 0u; checksum_index < payload_length; checksum_index++) // ⑧ 累加本段载荷到 8 位校验和
    {
        g_image_tx.legacy_checksum = (uint8_t)(g_image_tx.legacy_checksum +
            m_tx_packet[LEGACY_IMAGE_PACKET_HEADER_SIZE + checksum_index]);
    }
    radio_send_packet(m_tx_packet, false);                                    // ⑨ DATA后保持Radio关闭

    g_image_tx.block_sent = (uint16_t)(g_image_tx.block_sent + payload_length); // ⑩ 已发字节数累加
    ++g_image_tx.fragment_index;                                              // ⑪ 分片编号 +1
    g_image_tx.next_fragment_ms = g_time_ms + IMAGE_FRAGMENT_GAP_MS;          // ⑫ 下一片允许发送时间（节流 1 ms）
    if (g_image_tx.block_sent == g_image_tx.block_size)                       // ⑬ 本块全部发送完成
    {
        ++g_image_tx.block_pass_index;                                        //   已发遍数 +1
		/* 目前不需要用到 影响通信效率*/
        if (g_image_tx.block_pass_index < IMAGE_BLOCK_PASSES)                 // ⑭ 重复发送模式：还要再发一遍
        {
            g_image_tx.fragment_index = 0u;                                   //   分片号和已发字节清零，重新从 0 开始
            g_image_tx.block_sent = 0u;
            g_image_tx.next_fragment_ms = g_time_ms + IMAGE_PASS_GAP_MS;      //   等 IMAGE_PASS_GAP_MS 后再发第二遍
            return;                                                           //   返回，等下一轮再来发第二遍的第 0 片
        }

        memset(m_tx_packet, 0, sizeof(m_tx_packet));                          // ⑮ 准备 END 包：清零 buffer
        m_tx_packet[0] = LEGACY_CMD_IMAGE_END;                                //   命令字：图像结束包
        m_tx_packet[1] = (uint8_t)g_image_tx.frame_id;                        //   帧 ID
        memcpy(&m_tx_packet[2], g_capsule_sn, LEGACY_CAPSULE_SN_SIZE);        //   8 字节胶囊序列号
        m_tx_packet[10] = g_image_tx.legacy_checksum;                         //   整张图像的 8 位累加校验和
        radio_send_packet(m_tx_packet, g_image_tx.retry_count == 0u);          //   首轮END后才打开RX等待ACK
        g_image_tx.active = false;                                            // ⑯ 清活动标志，本帧数据已发完
        if (g_image_tx.retry_count == 0u)                                     // ⑰ 首次发送：等待接收端 ACK
        {
            g_image_tx.awaiting_ack = true;                                   //   置 ACK 等待标志
            g_image_tx.ack_deadline_ms = g_time_ms + IMAGE_ACK_TIMEOUT_MS;    //   设置 ACK 超时截止时间
        }
        else                                                                  // ⑱ 重发完成：不再等待第二次 ACK
        {
            g_image_tx.awaiting_ack = false;
            g_image_tx.retry_count = 0u;
            cx93510_host_suspend();                                           //   重发结束，关闭nRF侧SPIM0
        }
    }
}

/** @brief 处理 ACK 超时，并按原始协议重发 DATA 和 END（不重发 BEGIN）。 */
void image_ack_service(void)
{
    if (!g_image_tx.awaiting_ack ||                                           // ① 当前没在等 ACK：直接返回
        ((int32_t)(g_time_ms - g_image_tx.ack_deadline_ms) < 0))              // ② 还没到 ACK 超时截止时间：返回
    {
        return;
    }
    tx_runtime_log_ack_timeout(g_image_tx.frame_id,
                               g_image_tx.retry_count,
                               IMAGE_MAX_RETRIES);
    g_image_tx.awaiting_ack = false;                                          // ③ 清 ACK 等待标志（不管后面是否重发，都已超时）
    radio_enter_idle();                                                       //   ACK窗口结束，关闭Radio RX
    if (g_image_tx.retry_count >= IMAGE_MAX_RETRIES)                          // ④ 已达到最大重发次数：放弃本帧
    {
        g_image_tx.retry_count = 0u;
        cx93510_host_suspend();
        return;
    }
    g_image_tx.retry_count++;                                                 // ⑤ 重发计数 +1
    g_image_tx.legacy_checksum = 0u;                                          // ⑥ 重新计算累加校验和
    select_image_block(false);                                                // ⑦ 重新选择 JPEG 块（重置 offset/size/分片计数）
    g_image_tx.legacy_send_begin = false;                                     // ⑧ 关键：标记不是首轮，image_tx_service 不会重发 BEGIN
    g_image_tx.next_fragment_ms = g_time_ms;                                  // ⑨ 允许下一片立即发送
    g_image_tx.active = true;                                                 // ⑩ 重新置位活动标志，让 image_tx_service 进入重发流程
}
