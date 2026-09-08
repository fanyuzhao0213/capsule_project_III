/**
 * @file uart_bridge.c
 * @brief 专用接收板 UART 桥接实现
 *
 * 本文件从原 main.c 中剥离所有 UART 相关代码：高频时钟、TIMER2
 * 静默定时器、APP_UART FIFO、主循环字节收集与 STM 帧发送。
 */

#include "uart_bridge.h"
#include "config.h"
#include "image.h"
#include "nrf.h"
#include "nrf_gpio.h"
#include "nrf_log.h"
#include "nrf_log_ctrl.h"



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
    NRF_TIMER2->SHORTS = TIMER_SHORTS_COMPARE0_STOP_Msk;
    NRF_TIMER2->CC[0] = UART_RX_IDLE_TIMEOUT_US;
    NRF_TIMER2->EVENTS_COMPARE[0] = 0u;
    NRF_TIMER2->INTENCLR = 0xFFFFFFFFu;
    NRF_TIMER2->INTENSET = TIMER_INTENSET_COMPARE0_Msk;
    NVIC_ClearPendingIRQ(TIMER2_IRQn);
    NVIC_SetPriority(TIMER2_IRQn, UART_IDLE_TIMER_IRQ_PRIORITY);
    NVIC_EnableIRQ(TIMER2_IRQn);
}

/** @brief TIMER2 中断：置 500 ms 超时标志。 */
void TIMER2_IRQHandler(void)
{
    if (NRF_TIMER2->EVENTS_COMPARE[0] != 0u)
    {
        NRF_TIMER2->EVENTS_COMPARE[0] = 0u;
        g_uart_rx_idle_timeout = true;
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

            /* 新字节到达：取消旧超时，并重新开始完整的 500 ms 计时。 */
            g_uart_rx_idle_timeout = false;
            g_uart_rx_packet_active = true;
            NRF_TIMER2->TASKS_STOP = 1u;
            NRF_TIMER2->EVENTS_COMPARE[0] = 0u;
            NVIC_ClearPendingIRQ(TIMER2_IRQn);
            NRF_TIMER2->TASKS_CLEAR = 1u;
            NRF_TIMER2->TASKS_START = 1u;

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
    }
    else if (p_event->evt_type == APP_UART_FIFO_ERROR)
    {
        ++g_uart_rx_dropped;
    }
    else
    {
        /* APP_UART_TX_EMPTY 等事件不需要额外处理。 */
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
        NRF_LOG_INFO("UART RX packet complete: bytes=%u idle=500ms total=%u ring_dropped=%u packet_overflow=%u errors=%u",
                     (unsigned)m_uart_rx_packet_length,
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

        /* 打印结束后清空包缓冲区，等待 STM 发送下一包。 */
        m_uart_rx_packet_length = 0u;
        m_uart_rx_packet_overflow = 0u;
        did_work = true;
    }

    return did_work;
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

/** @brief 分批把待发送 STM 帧填入 SDK UART FIFO。 */
bool receiver_uart_tx_service(void)
{
    uint32_t budget = UART_TX_SERVICE_BUDGET;
    bool did_work = false;

    while ((g_stm_frame_length != 0u) &&
           (g_stm_frame_offset < g_stm_frame_length) &&
           (budget != 0u))
    {
        uint32_t result = app_uart_put(g_stm_frame[g_stm_frame_offset]);

        if (result == NRF_SUCCESS)
        {
            ++g_stm_frame_offset;
            --budget;
            did_work = true;
        }
        else if (result == NRF_ERROR_NO_MEM)
        {
            break;
        }
        else
        {
            NRF_LOG_ERROR("STM UART TX failed: offset=%u/%u error=%u",
                          (unsigned)g_stm_frame_offset,
                          (unsigned)g_stm_frame_length,
                          (unsigned)result);
            g_stm_frame_length = 0u;
            g_stm_frame_offset = 0u;
            return true;
        }
    }

    if ((g_stm_frame_length != 0u) &&
        (g_stm_frame_offset == g_stm_frame_length))
    {
        NRF_LOG_INFO("Legacy image forwarded to STM: id=%u bytes=%u",
                     g_stm_image_id, (unsigned)g_stm_image_length);
        g_stm_frame_length = 0u;
        g_stm_frame_offset = 0u;
        did_work = true;
    }
    return did_work;
}

