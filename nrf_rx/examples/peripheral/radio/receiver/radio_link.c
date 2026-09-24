/**
 * @file radio_link.c
 * @brief Radio硬件、收发切换和中断到主循环的包队列。
 */

#include "radio_link.h"
#include "antenna_manager.h"
#include "binding_storage.h"
#include "config.h"
#include "image.h"
#include "legacy_protocol.h"
#include "nrf.h"
#include "nrf_gpio.h"
#include "nrf_log.h"
#include "rf1662.h"
#include "uart_bridge.h"
#include <string.h>

typedef struct
{
    uint8_t packet[RADIO_PACKET_SIZE] __ALIGNED(4);      /** EasyDMA接收缓冲。 */
    uint8_t queue[RADIO_QUEUE_DEPTH][RADIO_PACKET_SIZE]; /** 主循环接收队列。 */
    volatile uint8_t head;                              /** 主循环读指针。 */
    volatile uint8_t tail;                              /** Radio中断写指针。 */
    volatile uint32_t received;                         /** 成功入队包数。 */
    volatile uint32_t dropped;                          /** 队列满丢包数。 */
    uint32_t processed;                                 /** 主循环处理包数。 */
} receiver_radio_state_t;

static receiver_radio_state_t m_radio;

/** @brief 返回接收模式使用的硬件快捷连接：READY自动接收并启动RSSI。 */
static uint32_t receiver_radio_rx_shorts(void)
{
    return RADIO_SHORTS_READY_START_Msk |
           RADIO_SHORTS_ADDRESS_RSSISTART_Msk;
}

/** @brief 等待Radio完全停止，供切天线和收发方向切换使用。 */
static void receiver_radio_disable(void)
{
    if (NRF_RADIO->STATE != RADIO_STATE_STATE_Disabled)
    {
        NRF_RADIO->EVENTS_DISABLED = 0u;
        NRF_RADIO->TASKS_DISABLE = 1u;
        while (NRF_RADIO->EVENTS_DISABLED == 0u) {}
    }
}

/** @brief 重新挂接EasyDMA接收缓冲并启动下一包接收。 */
static void receiver_radio_arm(void)
{
    NRF_RADIO->PACKETPTR = (uint32_t)m_radio.packet;
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

/** @brief 未绑定时仅保留SN广播及ZAYS控制帧，过滤无关图片流量。 */
static bool receiver_unbound_packet_is_needed(const uint8_t *packet)
{
    return (packet[0] == LEGACY_CMD_CAPSULE_SN_BROADCAST) ||
           ((packet[0] == 0x5Au) && (packet[1] == 0x41u) &&
            (packet[2] == 0x59u) && (packet[3] == 0x53u));
}

/** @brief 原子清空中断与主循环共享的无线接收队列。 */
void receiver_radio_clear_queue(void)
{
    uint32_t mask = __get_PRIMASK();
    __disable_irq();
    m_radio.head = 0u;
    m_radio.tail = 0u;
    if (mask == 0u)
    {
        __enable_irq();
    }
}

/** @brief 暂停Radio后切换RF1662天线，再恢复接收。 */
void receiver_radio_select_antenna(uint8_t antenna)
{
    NVIC_DisableIRQ(RADIO_IRQn);
    receiver_radio_disable();
    (void)rf1662_select_antenna(antenna);
    receiver_radio_arm();
    NVIC_ClearPendingIRQ(RADIO_IRQn);
    NVIC_EnableIRQ(RADIO_IRQn);
}

/** @brief 临时由接收切到发送，重复发送控制包后立即恢复接收。 */
bool receiver_radio_send(const uint8_t *packet, uint16_t length,
                         uint8_t repeat_count)
{
    uint8_t repeat;
    uint8_t tx_packet[RADIO_PACKET_SIZE] __ALIGNED(4) = {0};

    if ((packet == NULL) || (length == 0u) ||
        (length > RADIO_PACKET_SIZE) || (repeat_count == 0u))
    {
        return false;
    }

    memcpy(tx_packet, packet, length);
    /* 切换期间关闭Radio中断，避免ISR把发送完成误当成接收包。 */
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
    NRF_LOG_INFO("[RADIO TX] cmd=0x%02x repeat=%u ANT%u",
                 (unsigned)packet[0], (unsigned)repeat_count,
                 (unsigned)(rf1662_get_antenna() + 1u));
    return true;
}

/** @brief 按旧协议参数初始化2400 MHz、2 Mbps、固定长度Radio链路。 */
void receiver_radio_init(void)
{
    memset(&m_radio, 0, sizeof(m_radio));
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
    NRF_LOG_INFO("[RADIO] RX ready %uMHz 2Mbps packet=%u",
                 (unsigned)RADIO_FREQUENCY_MHZ,
                 (unsigned)RADIO_PACKET_SIZE);
}

/** @brief Radio包结束中断：快速分类、统计或入队，不在中断中解析业务。 */
void RADIO_IRQHandler(void)
{
    bool crc_ok;
    uint8_t tail;
    uint8_t next;

    if (NRF_RADIO->EVENTS_END == 0u)
    {
        return;
    }

    NRF_RADIO->EVENTS_END = 0u;
    NRF_RADIO->TASKS_RSSISTOP = 1u;
    crc_ok = (NRF_RADIO->CRCSTATUS != 0u);

    /* SEEK_END和FAST_SCAN包由天线模块直接消费，不进入图片队列。 */
    if (receiver_antenna_on_radio_packet(
            m_radio.packet, crc_ok, (uint8_t)NRF_RADIO->RSSISAMPLE))
    {
        receiver_radio_arm();
        return;
    }

    /* CRC错误包直接丢弃；未绑定时也不让连续图片占满队列。 */
    if (crc_ok &&
        (receiver_binding_is_bound() ||
         receiver_unbound_packet_is_needed(m_radio.packet)))
    {
        tail = m_radio.tail;
        next = (uint8_t)((tail + 1u) % RADIO_QUEUE_DEPTH);
        if (next != m_radio.head)
        {
            memcpy(m_radio.queue[tail], m_radio.packet, RADIO_PACKET_SIZE);
            m_radio.tail = next;
            ++m_radio.received;
        }
        else
        {
            ++m_radio.dropped;
        }
    }
    receiver_radio_arm();
}

/** @brief 主循环每次取一个无线包，先识别控制包，再交给图片模块。 */
bool receiver_radio_process_one(void)
{
    uint8_t head;

    if (m_radio.head == m_radio.tail)
    {
        return false;
    }

    head = m_radio.head;
	/*  true：这个包属于控制类，已经处理完毕。
		false：不是控制包，可以继续交给图片模块判断。*/
    if (!receiver_control_handle_radio_packet(m_radio.queue[head]))
    {
        receiver_image_process_packet(m_radio.queue[head]);
    }
    m_radio.head = (uint8_t)((head + 1u) % RADIO_QUEUE_DEPTH);
    ++m_radio.processed;

    if ((m_radio.processed & 0x1Fu) == 0u)
    {
        NRF_LOG_INFO("Bridge stats: radio_rx=%u processed=%u dropped=%u stm_pending=%u",
                     (unsigned)m_radio.received,
                     (unsigned)m_radio.processed,
                     (unsigned)m_radio.dropped,
                     (unsigned)receiver_uart_image_tx_busy());
    }
    return true;
}
