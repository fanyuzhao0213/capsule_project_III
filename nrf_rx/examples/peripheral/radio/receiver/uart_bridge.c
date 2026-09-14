/**
 * @file uart_bridge.c
 * @brief 专用接收板 UART 桥接实现
 *
 * 本文件从原 main.c 中剥离所有 UART 相关代码：高频时钟、TIMER2
 * 静默定时器、APP_UART FIFO、主循环字节收集与 STM 帧发送。
 */

#include "uart_bridge.h"
#include "binding_storage.h"
#include "config.h"
#include "image.h"
#include "legacy_protocol.h"
#include "nrf.h"
#include "nrf_gpio.h"
#include "nrf_log.h"
#include "nrf_log_ctrl.h"
#include <string.h>

#define CONTROL_DEBUG_LOG_ENABLED 1
#define UART_IDLE_PPI_RX_CHANNEL       7u

#if CONTROL_DEBUG_LOG_ENABLED
#define CONTROL_LOG_INFO(...)    NRF_LOG_INFO(__VA_ARGS__)
#define CONTROL_LOG_WARNING(...) NRF_LOG_WARNING(__VA_ARGS__)
#else
#define CONTROL_LOG_INFO(...)
#define CONTROL_LOG_WARNING(...)
#endif


/* ============================================================
 * 全局状态
 * ============================================================ */

/** UART RX 字节总数。 */
volatile uint32_t g_uart_rx_total;

/** UART RX 队列满丢包计数。 */
volatile uint32_t g_uart_rx_dropped;

/** UART 通信/FIFO 错误计数。 */
volatile uint32_t g_uart_rx_errors;

/** 主循环中当前正在拼装的 STM 数据包是否处于活动状态。 */
volatile bool g_uart_rx_packet_active;

/** 主循环中 500 ms UART 静默定时器超时标志。 */
volatile bool g_uart_rx_idle_timeout;

/** UART RX 环形缓冲区。 */
static uint8_t m_uart_rx_buffer[UART_RX_BUFFER_SIZE];

/** UART RX 环形缓冲区头指针（主循环读）。 */
static volatile uint16_t m_uart_rx_head;

/** UART RX 环形缓冲区尾指针（中断写）。 */
static volatile uint16_t m_uart_rx_tail;

/** 主循环拼装的当前 STM 数据包缓冲。 */
static uint8_t m_uart_rx_packet[UART_RX_PACKET_MAX_SIZE];

/** 主循环当前 STM 数据包已拼装长度。 */
static uint32_t m_uart_rx_packet_length;

/** 主循环当前 STM 数据包超出最大长度的丢弃计数。 */
static uint32_t m_uart_rx_packet_overflow;

/** 图片全部加入UART FIFO后，等待APP_UART_TX_EMPTY确认最后一字节已发出。 */
static volatile bool m_stm_image_waiting_tx_empty;
static volatile bool m_stm_image_tx_complete_due;
static bool m_control_response_seen;
static uint8_t m_expected_control_response;
static uint8_t m_pending_control_frame[LEGACY_CONTROL_FRAME_MAX_SIZE];
static uint16_t m_pending_control_length;

/** @brief 计算ZAYS控制帧数据区的8位累加校验和。 */
static uint8_t control_checksum(const uint8_t *data, uint32_t length)
{
    uint8_t sum = 0u;
    uint32_t index;
    for (index = 0u; index < length; ++index)
    {
        sum = (uint8_t)(sum + data[index]);
    }
    return sum;
}

/** @brief 组装ZAYS应答帧并通过UART发给STM32，最终由STM32透传PC。 */
static bool control_send_uart_frame(uint8_t command, const uint8_t *payload,
                                    uint16_t payload_length)
{
    uint8_t frame[32] = {0};
    uint16_t frame_length = (uint16_t)(8u + payload_length);
    if (frame_length > sizeof(frame))
    {
        return false;
    }
    frame[0] = 0x5Au;
    frame[1] = 0x41u;
    frame[2] = 0x59u;
    frame[3] = 0x53u;
    frame[4] = command;
    frame[5] = (uint8_t)(payload_length >> 8);
    frame[6] = (uint8_t)payload_length;
    if ((payload != NULL) && (payload_length != 0u))
    {
        memcpy(&frame[7], payload, payload_length);
    }
    frame[7u + payload_length] = control_checksum(&frame[7], payload_length);
    return receiver_uart_write(frame, frame_length);
}

/**
 * @brief 处理STM32发来的完整控制包。
 * @note 0x20～0x25在RX本地处理；0x40～0x47通过Radio转发给TX。
 */
static void control_handle_uart_packet(const uint8_t *frame, uint32_t length)
{
    uint32_t offset = 0u;
    uint16_t payload_length;
    while ((length - offset) >= LEGACY_CONTROL_FRAME_MIN_SIZE)
    {
        uint32_t frame_length;
        const uint8_t *current = &frame[offset];
        if ((current[0] != 0x5Au) || (current[1] != 0x41u) ||
            (current[2] != 0x59u) || (current[3] != 0x53u))
        {
            ++offset;
            continue;
        }
        payload_length = (uint16_t)(((uint16_t)current[5] << 8) | current[6]);
        frame_length = (uint32_t)payload_length + 8u;
        if ((frame_length > LEGACY_CONTROL_FRAME_MAX_SIZE) ||
            (frame_length > (length - offset)) ||
            (control_checksum(&current[7], payload_length) !=
             current[frame_length - 1u]))
        {
            CONTROL_LOG_WARNING("[CTRL] UART RX invalid: cmd=0x%02x len=%u bytes=%u",
                            (unsigned)current[4],
                            (unsigned)payload_length,
                            (unsigned)(length - offset));
            (void)control_send_uart_frame(0x49u, NULL, 0u);
            return;
        }
        CONTROL_LOG_INFO("[CTRL] UART RX OK: cmd=0x%02x data_len=%u frame_len=%u",
                     (unsigned)current[4],
                     (unsigned)payload_length,
                     (unsigned)frame_length);
        if ((current[4] == LEGACY_CMD_SN_QUERY_REQUEST) &&
            (payload_length == 0u))
        {
            uint8_t empty_sn[LEGACY_CAPSULE_SN_SIZE] = {0};
            const uint8_t *sn = receiver_binding_is_bound() ?
                                receiver_binding_get() : empty_sn;
            CONTROL_LOG_INFO("[BIND] query: bound=%u",
                             (unsigned)receiver_binding_is_bound());
            (void)control_send_uart_frame(LEGACY_CMD_SN_QUERY_RESPONSE,
                                          sn, LEGACY_CAPSULE_SN_SIZE);
            offset += frame_length;
            continue;
        }
        if ((current[4] == LEGACY_CMD_SN_UNBIND_REQUEST) &&
            (payload_length == 0u))
        {
            uint8_t result = receiver_binding_clear() ?
                             LEGACY_CONTROL_RESULT_OK :
                             LEGACY_CONTROL_RESULT_ERROR;
            CONTROL_LOG_INFO("[BIND] unbind result=%u", (unsigned)result);
            receiver_antenna_binding_changed();
            (void)control_send_uart_frame(LEGACY_CMD_SN_UNBIND_RESPONSE,
                                          &result, 1u);
            offset += frame_length;
            continue;
        }
        if ((current[4] == LEGACY_CMD_SN_BIND_REQUEST) &&
            (payload_length == LEGACY_CAPSULE_SN_SIZE))
        {
            uint8_t result = receiver_binding_set(&current[7]) ?
                             LEGACY_CONTROL_RESULT_OK :
                             LEGACY_CONTROL_RESULT_ERROR;
            CONTROL_LOG_INFO("[BIND] bind result=%u", (unsigned)result);
            receiver_antenna_binding_changed();
            NRF_LOG_HEXDUMP_INFO(&current[7], LEGACY_CAPSULE_SN_SIZE);
            (void)control_send_uart_frame(LEGACY_CMD_SN_BIND_RESPONSE,
                                          &result, 1u);
            offset += frame_length;
            continue;
        }
        m_control_response_seen = false;
        m_expected_control_response =
            (current[4] == LEGACY_CMD_SN_PREPARE_REQUEST) ?
             LEGACY_CMD_SN_PREPARE_RESPONSE :
            ((current[4] == LEGACY_CMD_DEVICE_ID_QUERY_REQUEST) ?
             LEGACY_CMD_DEVICE_ID_QUERY_RESPONSE :
            ((current[4] == LEGACY_CMD_SN_SET_REQUEST) ?
             LEGACY_CMD_SN_SET_RESPONSE :
            ((current[4] == LEGACY_CMD_SN_CONFIRM_REQUEST) ?
             LEGACY_CMD_SN_CONFIRM_RESPONSE : 0u)));
        if (m_expected_control_response == 0u)
        {
            CONTROL_LOG_WARNING("[CTRL] unsupported local command=0x%02x",
                                (unsigned)current[4]);
            (void)control_send_uart_frame(LEGACY_CMD_CONTROL_ERROR_RESPONSE,
                                          NULL, 0u);
            offset += frame_length;
            continue;
        }
        CONTROL_LOG_INFO("[CTRL] RADIO TX: cmd=0x%02x repeat=3",
                     (unsigned)current[4]);
        receiver_send_control_packet(current, (uint16_t)frame_length, 3u);
        offset += frame_length;
    }
}



/* ============================================================
 * 时钟与定时器
 * ============================================================ */

/** @brief 启动 16 MHz 高频晶振供 Radio 使用。 */
void receiver_clock_init(void)
{
    NRF_CLOCK->EVENTS_HFCLKSTARTED = 0u;
    NRF_CLOCK->TASKS_HFCLKSTART = 1u;
    while (NRF_CLOCK->EVENTS_HFCLKSTARTED == 0u) {}
}

/** @brief 初始化 TIMER2 作为 500 ms UART 静默定时器。 */
void receiver_uart_idle_timer_init(void)
{
    NRF_TIMER2->TASKS_STOP = 1u;
    NRF_TIMER2->MODE = TIMER_MODE_MODE_Timer;
    NRF_TIMER2->BITMODE = TIMER_BITMODE_BITMODE_32Bit;
    NRF_TIMER2->PRESCALER = 4u;
    /* Keep the idle timer running periodically. RXDRDY clears it through PPI,
     * so COMPARE0 always means that no byte arrived for 500 ms. Keeping it
     * running avoids depending on a PPI FORK task to restart a stopped timer. */
    NRF_TIMER2->SHORTS = TIMER_SHORTS_COMPARE0_CLEAR_Msk;
    NRF_TIMER2->CC[0] = UART_RX_IDLE_TIMEOUT_US;
    NRF_TIMER2->EVENTS_COMPARE[0] = 0u;
    NRF_TIMER2->INTENCLR = 0xFFFFFFFFu;
    NRF_TIMER2->INTENSET = TIMER_INTENSET_COMPARE0_Msk;
    NVIC_ClearPendingIRQ(TIMER2_IRQn);
    NVIC_SetPriority(TIMER2_IRQn, UART_IDLE_TIMER_IRQ_PRIORITY);
    NVIC_EnableIRQ(TIMER2_IRQn);

    /* Hardware idle detection: every physical RX byte clears/starts TIMER2.
     * After 500 ms silence TIMER2 stops UARTE RX, which makes nrfx deliver
     * the partially filled DMA block without a per-byte CPU interrupt. */
    NRF_PPI->CHENCLR = (1u << UART_IDLE_PPI_RX_CHANNEL);
    NRF_PPI->CH[UART_IDLE_PPI_RX_CHANNEL].EEP =
        (uint32_t)&NRF_UARTE0->EVENTS_RXDRDY;
    NRF_PPI->CH[UART_IDLE_PPI_RX_CHANNEL].TEP =
        (uint32_t)&NRF_TIMER2->TASKS_CLEAR;
    NRF_PPI->FORK[UART_IDLE_PPI_RX_CHANNEL].TEP = 0u;
    NRF_PPI->CHENSET = (1u << UART_IDLE_PPI_RX_CHANNEL);
    NRF_TIMER2->TASKS_CLEAR = 1u;
    NRF_TIMER2->TASKS_START = 1u;
}

/** @brief TIMER2 中断：置 500 ms 超时标志。 */
void TIMER2_IRQHandler(void)
{
    uint32_t dma_amount;
    if (NRF_TIMER2->EVENTS_COMPARE[0] != 0u)
    {
        NRF_TIMER2->EVENTS_COMPARE[0] = 0u;
        dma_amount = NRF_UARTE0->RXD.AMOUNT;
        /* RXDRDY clears TIMER2 for every physical byte, therefore this compare
         * occurs 100 ms after the last byte. Stop a partial DMA block first;
         * an exact 8-byte block has already reached the software ring. */
        if (dma_amount != 0u)
        {
            NRF_UARTE0->SHORTS &= ~UARTE_SHORTS_ENDRX_STARTRX_Msk;
            NRF_UARTE0->TASKS_STOPRX = 1u;
        }
        if (g_uart_rx_packet_active || (dma_amount != 0u))
        {
            g_uart_rx_idle_timeout = true;
        }
    }
}



/* ============================================================
 * UART 事件与初始化
 * ============================================================ */

/** @brief APP_UART 事件处理：取出 RX FIFO 字节入环形缓冲，重启 500 ms 静默计时。 */
static void receiver_uart_event_handler(app_uart_evt_t *p_event)
{
    if (p_event->evt_type == APP_UART_DATA_READY)
    {
        uint8_t byte;
        while (app_uart_get(&byte) == NRF_SUCCESS)
        {
            uint16_t tail;
            uint16_t next;

            ++g_uart_rx_total;

            /* RXDRDY -> TIMER2 is handled by PPI, so this callback only moves
             * the completed DMA block into the software ring buffer. */
            g_uart_rx_idle_timeout = false;
            g_uart_rx_packet_active = true;

            tail = m_uart_rx_tail;
            next = (uint16_t)((tail + 1u) & UART_RX_BUFFER_MASK);
            if (next != m_uart_rx_head)
            {
                m_uart_rx_buffer[tail] = byte;
                m_uart_rx_tail = next;
            }
            else
            {
                ++g_uart_rx_dropped;
            }
        }
    }
    else if (p_event->evt_type == APP_UART_COMMUNICATION_ERROR)
    {
        ++g_uart_rx_errors;
        CONTROL_LOG_WARNING("[UART] communication error: mask=0x%08x total=%u (01=overrun 02=parity 04=framing 08=break)",
                            (unsigned)p_event->data.error_communication,
                            (unsigned)g_uart_rx_errors);
    }
    else if (p_event->evt_type == APP_UART_FIFO_ERROR)
    {
        ++g_uart_rx_dropped;
        CONTROL_LOG_WARNING("[UART] FIFO error: code=%u dropped=%u",
                            (unsigned)p_event->data.error_code,
                            (unsigned)g_uart_rx_dropped);
    }
    else if (p_event->evt_type == APP_UART_TX_EMPTY)
    {
        if (m_stm_image_waiting_tx_empty)
        {
            m_stm_image_tx_complete_due = true;
        }
    }
    else
    {
        /* 其余事件不需要处理。 */
    }
}

/** @brief 初始化 APP_UART 1 Mbps 8N1。 */
void receiver_uart_init(void)
{
    uint32_t error;
    const app_uart_comm_params_t params =
    {
        UART_RX_PIN,
        UART_TX_PIN,
        UART_PIN_DISCONNECTED,
        UART_PIN_DISCONNECTED,
        APP_UART_FLOW_CONTROL_DISABLED,
        false,
        UART_BAUDRATE_BAUDRATE_Baud1M
    };

    APP_UART_FIFO_INIT(&params,
                       UART_RX_BUFFER_SIZE,
                       UART_TX_FIFO_SIZE,
                       receiver_uart_event_handler,
                       (app_irq_priority_t)UART_IRQ_PRIORITY,
                       error);
    APP_ERROR_CHECK(error);

    NRF_LOG_INFO("APP_UART ready: 1 Mbps 8N1, RX=P0.%02u TX=P0.%02u, RX event enabled",
                 (unsigned)UART_RX_PIN, (unsigned)UART_TX_PIN);
}

/** @brief 初始化 P0.23 外部模式控制脚，接收期间输出低电平。 */
void receiver_mode_pin_init(void)
{
    nrf_gpio_cfg_output(RECEIVER_MODE_PIN);
    nrf_gpio_pin_clear(RECEIVER_MODE_PIN);
    NRF_LOG_INFO("Receiver mode control: P0.%02u LOW",
                 (unsigned)RECEIVER_MODE_PIN);
}



/* ============================================================
 * UART 数据处理
 * ============================================================ */

/** @brief 主循环收集 UART 字节并在静默超时后通过 RTT 打印整包。 */
bool receiver_uart_process_received(void)
{
    bool did_work = false;
    bool packet_complete = false;
    uint16_t head = m_uart_rx_head;

    /* 第一步：将中断环形缓冲区中的全部字节拼接到当前 STM 数据包。 */
    while (head != m_uart_rx_tail)
    {
        uint8_t byte = m_uart_rx_buffer[head];
        head = (uint16_t)((head + 1u) & UART_RX_BUFFER_MASK);
        if (m_uart_rx_packet_length < sizeof(m_uart_rx_packet))
        {
            m_uart_rx_packet[m_uart_rx_packet_length++] = byte;
        }
        else
        {
            ++m_uart_rx_packet_overflow;
        }
        did_work = true;
    }
    m_uart_rx_head = head;

    /* 第二步：超时标志出现后短暂关中断复查，确认环形队列空才结束当前包。 */
    if (g_uart_rx_packet_active && g_uart_rx_idle_timeout)
    {
        __disable_irq();
        if (g_uart_rx_packet_active && g_uart_rx_idle_timeout &&
            (m_uart_rx_head == m_uart_rx_tail))
        {
            g_uart_rx_packet_active = false;
            g_uart_rx_idle_timeout = false;
            packet_complete = true;
        }
        __enable_irq();
    }

    /* 第三步：包结束后由 while(1) 上下文打印，不占用 UART 或 TIMER 中断时间。 */
    if (packet_complete)
    {
        uint32_t offset;
        NRF_LOG_INFO("UART RX packet complete: bytes=%u idle=%ums total=%u ring_dropped=%u packet_overflow=%u errors=%u",
                     (unsigned)m_uart_rx_packet_length,
                     (unsigned)(UART_RX_IDLE_TIMEOUT_US / 1000u),
                     (unsigned)g_uart_rx_total,
                     (unsigned)g_uart_rx_dropped,
                     (unsigned)m_uart_rx_packet_overflow,
                     (unsigned)g_uart_rx_errors);
        NRF_LOG_FLUSH();

        for (offset = 0u; offset < m_uart_rx_packet_length;
             offset += UART_RX_LOG_CHUNK)
        {
            uint32_t remaining = m_uart_rx_packet_length - offset;
            uint32_t chunk = (remaining > UART_RX_LOG_CHUNK) ?
                             UART_RX_LOG_CHUNK : remaining;
            NRF_LOG_HEXDUMP_INFO(&m_uart_rx_packet[offset], chunk);
            NRF_LOG_FLUSH();
        }

        control_handle_uart_packet(m_uart_rx_packet, m_uart_rx_packet_length);

        /* 打印结束后清空包缓冲区，等待 STM 发送下一包。 */
        m_uart_rx_packet_length = 0u;
        m_uart_rx_packet_overflow = 0u;
        did_work = true;
    }

    return did_work;
}

/**
 * @brief 处理Radio收到的SN广播或TX控制应答，并按绑定SN过滤广播。
 * @return true表示该包已作为控制业务处理，false表示应交给图片解析器。
 */
bool receiver_control_handle_radio_packet(const uint8_t *packet)
{
    uint16_t payload_length;
    uint16_t frame_length;
    if (packet == NULL)
    {
        return false;
    }
    if ((packet[0] == LEGACY_CMD_CAPSULE_SN_BROADCAST))
    {
        if (!receiver_binding_is_bound())
        {
            CONTROL_LOG_INFO("[DISCOVERY] SN received (8 bytes):");
            NRF_LOG_HEXDUMP_INFO(&packet[1], LEGACY_CAPSULE_SN_SIZE);
            receiver_antenna_note_discovery_sn();
        }
        if (receiver_binding_is_bound() &&
            !receiver_binding_matches(&packet[1]))
        {
            return true;
        }
        /* 先提取8字节有效SN；即使图片UART正忙，也不能丢失设备信息。 */
        receiver_device_info_update_capsule_sn(&packet[1]);
        if (g_stm_frame_length != 0u)
        {
            CONTROL_LOG_INFO("[CTRL] SN broadcast dropped: image UART busy");
            return true;
        }
        CONTROL_LOG_INFO("[CTRL] RADIO RX broadcast: cmd=0x05; UART TX len=16");
        return control_send_uart_frame(LEGACY_CMD_CAPSULE_SN_BROADCAST,
                                       &packet[1], LEGACY_CAPSULE_SN_SIZE);
    }
    if ((packet[0] != 0x5Au) || (packet[1] != 0x41u) ||
        (packet[2] != 0x59u) || (packet[3] != 0x53u))
    {
        return false;
    }
    payload_length = (uint16_t)(((uint16_t)packet[5] << 8) | packet[6]);
    frame_length = (uint16_t)(payload_length + 8u);
    if ((frame_length > LEGACY_CONTROL_FRAME_MAX_SIZE) ||
        (control_checksum(&packet[7], payload_length) !=
         packet[frame_length - 1u]))
    {
        CONTROL_LOG_WARNING("[CTRL] RADIO RX invalid: cmd=0x%02x data_len=%u",
                        (unsigned)packet[4], (unsigned)payload_length);
        return true;
    }
    CONTROL_LOG_INFO("[CTRL] RADIO RX OK: cmd=0x%02x data_len=%u frame_len=%u",
                 (unsigned)packet[4],
                 (unsigned)payload_length,
                 (unsigned)frame_length);
    if ((m_expected_control_response != 0u) &&
        (packet[4] != m_expected_control_response) &&
        (packet[4] != LEGACY_CMD_CONTROL_ERROR_RESPONSE))
    {
        CONTROL_LOG_WARNING("[CTRL] response ignored: expected=0x%02x received=0x%02x",
                        (unsigned)m_expected_control_response,
                        (unsigned)packet[4]);
        return true;
    }
    if (m_control_response_seen)
    {
        CONTROL_LOG_INFO("[CTRL] duplicate response ignored: cmd=0x%02x",
                     (unsigned)packet[4]);
        return true;
    }
    m_control_response_seen = true;
    if (g_stm_frame_length != 0u)
    {
        memcpy(m_pending_control_frame, packet, frame_length);
        m_pending_control_length = frame_length;
        CONTROL_LOG_INFO("[CTRL] UART TX queued: cmd=0x%02x image busy",
                     (unsigned)packet[4]);
    }
    else
    {
        CONTROL_LOG_INFO("[CTRL] UART TX: cmd=0x%02x frame_len=%u",
                     (unsigned)packet[4], (unsigned)frame_length);
        (void)receiver_uart_write(packet, frame_length);
    }
    return true;
}

/** @brief 在图片UART发送空闲后补发排队的短控制应答。 */
bool receiver_control_service(void)
{
    if ((m_pending_control_length == 0u) || (g_stm_frame_length != 0u))
    {
        return false;
    }
    if (receiver_uart_write(m_pending_control_frame,
                            m_pending_control_length))
    {
        CONTROL_LOG_INFO("[CTRL] queued UART TX complete: cmd=0x%02x frame_len=%u",
                     (unsigned)m_pending_control_frame[4],
                     (unsigned)m_pending_control_length);
        m_pending_control_length = 0u;
    }
    return true;
}

/** @brief 将一段 UART 二进制数据写入 SDK TX FIFO（FIFO 满时短暂阻塞重试）。 */
bool receiver_uart_write(const uint8_t *data, uint32_t length)
{
    uint32_t i;
    for (i = 0u; i < length; ++i)
    {
        uint32_t timeout = 2000000u;
        uint32_t result;
        do
        {
            result = app_uart_put(data[i]);
        }
        while ((result == NRF_ERROR_NO_MEM) && (--timeout != 0u));

        if ((result != NRF_SUCCESS) || (timeout == 0u))
        {
            NRF_LOG_ERROR("APP_UART TX failed at byte %u/%u, error=%u",
                          (unsigned)i, (unsigned)length, (unsigned)result);
            NRF_LOG_FLUSH();
            return false;
        }
    }
    return true;
}

/** @brief 用EasyDMA分块发送STM帧；每块完成后由TX_EMPTY启动下一块。 */
bool receiver_uart_tx_service(void)
{
    uint32_t remaining;
    uint16_t chunk_length;
    uint32_t result;

    if (m_stm_image_tx_complete_due)
    {
        m_stm_image_tx_complete_due = false;
        m_stm_image_waiting_tx_empty = false;
        if ((g_stm_frame_length != 0u) &&
            (g_stm_frame_offset == g_stm_frame_length))
        {
            receiver_note_stm_forwarded(g_stm_image_id, g_stm_image_length);
            g_stm_frame_length = 0u;
            g_stm_frame_offset = 0u;
            return true;
        }
    }

    if (m_stm_image_waiting_tx_empty || (g_stm_frame_length == 0u))
    {
        return false;
    }

    remaining = g_stm_frame_length - g_stm_frame_offset;
    chunk_length = (remaining > UART_TX_DMA_CHUNK_SIZE) ?
                   (uint16_t)UART_TX_DMA_CHUNK_SIZE : (uint16_t)remaining;

    /* 对很短的末块也消除“DMA已结束但waiting尚未置位”的中断竞态。 */
    __disable_irq();
    result = app_uart_tx_buffer(&g_stm_frame[g_stm_frame_offset],
                                chunk_length);
    if (result == NRF_SUCCESS)
    {
        g_stm_frame_offset += chunk_length;
        m_stm_image_waiting_tx_empty = true;
    }
    __enable_irq();

    if (result == NRF_SUCCESS)
    {
        return true;
    }
    if ((result == NRF_ERROR_BUSY) || (result == NRF_ERROR_NO_MEM))
    {
        return false;
    }

    NRF_LOG_ERROR("STM UART DMA failed: offset=%u/%u chunk=%u error=%u",
                  (unsigned)g_stm_frame_offset,
                  (unsigned)g_stm_frame_length,
                  (unsigned)chunk_length,
                  (unsigned)result);
    g_stm_frame_length = 0u;
    g_stm_frame_offset = 0u;
    return true;
}
