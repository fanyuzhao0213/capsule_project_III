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
#include "nrf.h"
#include "nrf_gpio.h"
#include "nrf_log.h"
#include <string.h>

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



/* ============================================================
 * 全局状态
 * ============================================================ */

/** 图像发送状态机全局实例。 */
image_tx_t g_image_tx;

/** 1 ms 系统时间戳。 */
volatile uint32_t g_time_ms;

/** 图像采集请求标志，由 TIMER1 周期置位。 */
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

/* ============================================================
 * 序列号与初始化
 * ============================================================ */

/** @brief 重新将 g_capsule_sn 同步到 capsule_sn_storage_get_active() 指向的数据。 */
void capsule_sn_refresh(void)
{
    const uint8_t *active_sn = capsule_sn_storage_get_active();
    memcpy(g_capsule_sn, active_sn, LEGACY_CAPSULE_SN_SIZE);
}

void capsule_sn_config_window_begin(void)
{
    m_sn_update_pending = false;
    m_sn_config_complete = false;
    m_sn_config_window_active = true;
    m_sn_config_last_activity_ms = g_time_ms;
    NRF_LOG_INFO("[SN CONFIG] window OPEN");
}

void capsule_sn_config_window_end(void)
{
    m_sn_config_window_active = false;
    m_sn_update_pending = false;
    NRF_LOG_INFO("[SN CONFIG] window CLOSED: result=%s",
                 m_sn_config_complete ? "configured" : "timeout");
}

bool capsule_sn_config_is_complete(void)
{
    return m_sn_config_complete;
}

uint32_t capsule_sn_config_last_activity_ms(void)
{
    return m_sn_config_last_activity_ms;
}

/** @brief 启动 Radio 与 CX93510 所需的 16 MHz 外部高频晶振。 */
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
    NRF_RADIO->SHORTS = RADIO_SHORTS_READY_START_Msk;
    NRF_RADIO->INTENSET = RADIO_INTENSET_END_Msk;
    NVIC_ClearPendingIRQ(RADIO_IRQn);
    NVIC_SetPriority(RADIO_IRQn, RADIO_IRQ_PRIORITY);
    NVIC_EnableIRQ(RADIO_IRQn);
    radio_arm_rx();
}

/** @brief 阻塞发送一个 254 字节 Radio 包，发送完成立即恢复 RX。 */
static void radio_send_packet(const uint8_t *packet)
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
    NRF_RADIO->SHORTS = RADIO_SHORTS_READY_START_Msk;
    radio_arm_rx();
    NVIC_ClearPendingIRQ(RADIO_IRQn);
    NVIC_EnableIRQ(RADIO_IRQn);
}

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
        radio_send_packet(m_tx_packet);
    }
}

static bool radio_process_control_frame(const uint8_t *packet)
{
    uint16_t payload_length;
    uint16_t frame_length;
    capsule_sn_t requested_sn;
    if ((packet[0] != 0x5Au) || (packet[1] != 0x41u) ||
        (packet[2] != 0x59u) || (packet[3] != 0x53u))
    {
        return false;
    }
    payload_length = LegacyProtocol_GetU16Be(&packet[5]);
    frame_length = (uint16_t)(payload_length + 8u);
    NRF_LOG_INFO("[CTRL] RADIO RX request: cmd=0x%02x data_len=%u frame_len=%u",
                 (unsigned)packet[4],
                 (unsigned)payload_length,
                 (unsigned)frame_length);
    NRF_LOG_HEXDUMP_INFO(packet,
                         (frame_length <= LEGACY_CONTROL_FRAME_MAX_SIZE) ?
                         frame_length : LEGACY_CONTROL_FRAME_MAX_SIZE);
    if ((frame_length > LEGACY_CONTROL_FRAME_MAX_SIZE) ||
        (control_checksum(&packet[7], payload_length) !=
         packet[frame_length - 1u]))
    {
        NRF_LOG_WARNING("[CTRL] invalid frame: cmd=0x%02x len=%u",
                        (unsigned)packet[4],
                        (unsigned)payload_length);
        return true;
    }
    if (((packet[4] == LEGACY_CMD_SN_PREPARE_REQUEST) ||
         (packet[4] == LEGACY_CMD_DEVICE_ID_QUERY_REQUEST) ||
         (packet[4] == LEGACY_CMD_SN_SET_REQUEST) ||
         (packet[4] == LEGACY_CMD_SN_CONFIRM_REQUEST)) &&
        !m_sn_config_window_active)
    {
        /* A repeated confirm can arrive because NRF_RX transmits each request
         * three times. Keep the successful confirm idempotent. */
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
    if ((packet[4] == LEGACY_CMD_SN_PREPARE_REQUEST) &&
             (payload_length == 0u))
    {
        m_sn_update_pending = false;
        NRF_LOG_INFO("[CTRL] factory SN prepare accepted; pending cleared");
        radio_send_control_response(LEGACY_CMD_SN_PREPARE_RESPONSE,
                                    NULL, 0u);
    }
    else if ((packet[4] == LEGACY_CMD_DEVICE_ID_QUERY_REQUEST) &&
             (payload_length == 0u))
    {
        NRF_LOG_INFO("[CTRL] DEVICEID query");
        NRF_LOG_HEXDUMP_INFO((const uint8_t *)NRF_FICR->DEVICEID, 8u);
        radio_send_control_response(LEGACY_CMD_DEVICE_ID_QUERY_RESPONSE,
                                    (const uint8_t *)NRF_FICR->DEVICEID, 8u);
    }
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
                capsule_sn_refresh();
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

void capsule_sn_broadcast_service(void)
{
    static uint32_t last_broadcast_ms;
    if (g_image_tx.active || g_image_tx.awaiting_ack ||
        ((last_broadcast_ms != 0u) &&
         ((g_time_ms - last_broadcast_ms) < SN_BROADCAST_PERIOD_MS)))
    {
        return;
    }
    last_broadcast_ms = g_time_ms;
    memset(m_tx_packet, 0, sizeof(m_tx_packet));
    m_tx_packet[0] = LEGACY_CMD_CAPSULE_SN_BROADCAST;
    memcpy(&m_tx_packet[1], g_capsule_sn, LEGACY_CAPSULE_SN_SIZE);
    radio_send_packet(m_tx_packet);
}

void capsule_sn_broadcast_burst(uint8_t repeat_count)
{
    uint8_t index;
    memset(m_tx_packet, 0, sizeof(m_tx_packet));
    m_tx_packet[0] = LEGACY_CMD_CAPSULE_SN_BROADCAST;
    memcpy(&m_tx_packet[1], g_capsule_sn, LEGACY_CAPSULE_SN_SIZE);
    for (index = 0u; index < repeat_count; ++index)
    {
        radio_send_packet(m_tx_packet);
    }
}



/* ============================================================
 * LED 控制
 * ============================================================ */

/** @brief 初始化 P0.08 发送指示灯，默认低电平（熄灭）。 */
void image_tx_led_init(void)
{
    nrf_gpio_cfg_output(IMAGE_TX_LED_PIN);
    nrf_gpio_pin_clear(IMAGE_TX_LED_PIN);
}

/** @brief 控制 P0.08 发送指示灯亮灭。 */
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
        else if (g_image_tx.awaiting_ack &&                                  // ② 正在等待 ACK 且命令字匹配
            (packet[0] == LEGACY_CMD_IMAGE_RECEIVED_RESPONSE) &&
            (packet[1] == (uint8_t)g_image_tx.frame_id) &&                   // ③ 帧 ID 必须一致（防止旧 ACK 误清当前等待）
            (memcmp(&packet[2], g_capsule_sn, LEGACY_CAPSULE_SN_SIZE) == 0))  // ④ 序列号必须一致（防止其他设备的 ACK 干扰）
        {
            g_image_tx.awaiting_ack = false;                                  // ⑤ ACK 匹配成功：清等待标志
            g_image_tx.retry_count = 0u;                                      //   清重发计数
        }
        m_rx_queue.head = (uint8_t)((head + 1u) % RADIO_QUEUE_DEPTH);         // ⑥ 无论是否匹配 ACK，都要把这包出队，否则会卡队列
    }
}



/* ============================================================
 * TIMER1 1 ms 周期中断
 * ============================================================ */

/** @brief TIMER1 1 ms 周期中断：累加时间戳，每 IMAGE_PERIOD_MS 置采集标志。 */
void TIMER1_IRQHandler(void)
{
    static uint16_t image_period_count;
    if (NRF_TIMER1->EVENTS_COMPARE[0] != 0u)
    {
        NRF_TIMER1->EVENTS_COMPARE[0] = 0u;
        ++g_time_ms;
        if (++image_period_count >= IMAGE_PERIOD_MS)
        {
            image_period_count = 0u;
#if IMAGE_TRANSMISSION_ENABLED
            g_capture_due = true;
#endif
        }
    }
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

/** @brief 处理每 IMAGE_PERIOD_MS 一次的图像采集请求。 */
void image_capture_task(void)
{
    if (!g_capture_due)                                                          // ① TIMER1 周期未到：直接返回，不抢占主循环
    {
        return;       //没到周期直接返回
    }
    g_capture_due = false;                                                       // ② 立刻清标志，避免下一轮主循环重复进入采集流程
    if (g_image_tx.active || g_image_tx.awaiting_ack)                           // ③ 上一帧还在发或还在等 ACK：放弃本周期，防止 CX93510 帧缓冲被新帧覆盖
    {
        return;                                                                  //   静默跳过，不再打印 WARNING（避免日志刷屏）
    }
    if (!cx93510_capture_one(&g_image_tx.frame, IMAGE_CAPTURE_TIMEOUT_MS))      // ⑤ 调用 CX93510 采集一帧并在超时内读回帧头（offset/size）
    {
        image_tx_led_set(false);
        return;
    }
    if ((g_image_tx.frame.jpeg_size == 0u) ||                                   // ⑥ 过滤异常帧：JPEG 长度为 0 或超出协议上限的帧直接丢弃
        (g_image_tx.frame.jpeg_size > LEGACY_IMAGE_MAX_SIZE))
    {
        image_tx_led_set(false);
        return;
    }
    g_image_tx.frame_id = (uint16_t)((g_image_tx.frame_id + 1u) % 255u);        // ⑦ 帧 ID 自增 1（0~255 循环），供接收端区分不同帧
    g_image_tx.legacy_checksum = 0u;                                            // ⑧ 清零本帧 JPEG 8 位累加校验和
    g_image_tx.retry_count = 0u;                                                // ⑨ 清零本帧已重发计数
    g_image_tx.legacy_send_begin = true;                                        // ⑩ 置首轮标志，让 image_tx_service 在第 0 片连发两次 BEGIN 包
    select_image_block(false);                                                  // ⑪ 选中 JPEG 块（不是 config 块），写入 offset/size 并算出总分片数
    g_image_tx.next_fragment_ms = g_time_ms;                                    // ⑫ 允许下一分片立即发送（image_tx_service 不会等到未来时刻）
    g_image_tx.active = true;                                                   // ⑬ 置位活动标志，通知 image_tx_service 开始处理本帧
    image_tx_led_set(true);                                                     // ⑭ 点亮 P0.08 发送指示灯，提示用户正在无线发送图像
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
        radio_send_packet(m_tx_packet);                                          //   第 1 次发 BEGIN
        radio_send_packet(m_tx_packet);                                          //   第 2 次发 BEGIN（抗丢包）
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
        return;
    }
    for (checksum_index = 0u; checksum_index < payload_length; checksum_index++) // ⑧ 累加本段载荷到 8 位校验和
    {
        g_image_tx.legacy_checksum = (uint8_t)(g_image_tx.legacy_checksum +
            m_tx_packet[LEGACY_IMAGE_PACKET_HEADER_SIZE + checksum_index]);
    }
    radio_send_packet(m_tx_packet);                                           // ⑨ 真正发送 DATA 包（一次循环只发一包）

    g_image_tx.block_sent = (uint16_t)(g_image_tx.block_sent + payload_length); // ⑩ 已发字节数累加
    ++g_image_tx.fragment_index;                                              // ⑪ 分片编号 +1
    g_image_tx.next_fragment_ms = g_time_ms + IMAGE_FRAGMENT_GAP_MS;          // ⑫ 下一片允许发送时间（节流 1 ms）
    if (g_image_tx.block_sent == g_image_tx.block_size)                       // ⑬ 本块全部发送完成
    {
        ++g_image_tx.block_pass_index;                                        //   已发遍数 +1
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
        radio_send_packet(m_tx_packet);                                       //   发送 END 包
        image_tx_led_set(false);                                              //   熄灭 P0.08 发送指示灯
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
    g_image_tx.awaiting_ack = false;                                          // ③ 清 ACK 等待标志（不管后面是否重发，都已超时）
    if (g_image_tx.retry_count >= IMAGE_MAX_RETRIES)                          // ④ 已达到最大重发次数：放弃本帧
    {
        g_image_tx.retry_count = 0u;
        return;
    }
    g_image_tx.retry_count++;                                                 // ⑤ 重发计数 +1
    g_image_tx.legacy_checksum = 0u;                                          // ⑥ 重新计算累加校验和
    select_image_block(false);                                                // ⑦ 重新选择 JPEG 块（重置 offset/size/分片计数）
    g_image_tx.legacy_send_begin = false;                                     // ⑧ 关键：标记不是首轮，image_tx_service 不会重发 BEGIN
    g_image_tx.next_fragment_ms = g_time_ms;                                  // ⑨ 允许下一片立即发送
    g_image_tx.active = true;                                                 // ⑩ 重新置位活动标志，让 image_tx_service 进入重发流程
    image_tx_led_set(true);                                                   // ⑪ 点亮 P0.08 指示灯
}
