/**
 * @file uart_bridge.c
 * @brief UART硬件和RX与STM32之间的协议桥接。
 */

#include "uart_bridge.h"
#include "antenna_manager.h"
#include "app_error.h"
#include "app_uart.h"
#include "binding_storage.h"
#include "config.h"
#include "control_stream_parser.h"
#include "image.h"
#include "legacy_protocol.h"
#include "nrf.h"
#include "nrf_gpio.h"
#include "nrf_log.h"
#include "radio_link.h"
#include "rf1662.h"
#include <string.h>

#define CONTROL_DEBUG_LOG_ENABLED 1u
#define UART_IDLE_PPI_RX_CHANNEL  7u
#define STM_IMAGE_FRAME_MAX_SIZE  (LEGACY_STM_FRAME_HEADER_SIZE + \
                                   LEGACY_DEVICE_INFO_SIZE + \
                                   LEGACY_IMAGE_MAX_SIZE + 1u)

#if CONTROL_DEBUG_LOG_ENABLED
#define CONTROL_LOG_INFO(...)    NRF_LOG_INFO(__VA_ARGS__)
#define CONTROL_LOG_WARNING(...) NRF_LOG_WARNING(__VA_ARGS__)
#else
#define CONTROL_LOG_INFO(...)
#define CONTROL_LOG_WARNING(...)
#endif

typedef struct
{
    uint8_t ring[UART_RX_BUFFER_SIZE];                   /** UART中断到主循环的接收环形缓冲区。 */
    volatile uint16_t ring_head;                        /** 主循环读取环形缓冲区的位置。 */
    volatile uint16_t ring_tail;                        /** UART中断写入环形缓冲区的位置。 */
    uint8_t packet[UART_RX_PACKET_MAX_SIZE];            /** 按静默间隔组装的UART数据包缓冲区。 */
    uint32_t packet_length;                             /** 当前UART数据包已组装长度，单位字节。 */
    uint32_t packet_overflow;                           /** 当前UART数据包超出缓冲区的字节数。 */
    volatile bool packet_active;                        /** 当前是否正在接收一个UART数据包。 */
    volatile bool idle_timeout;                         /** UART静默超时是否已经到达。 */
    volatile uint32_t received_bytes;                   /** UART累计接收字节数。 */
    volatile uint32_t dropped_bytes;                    /** 环形缓冲区满时累计丢弃字节数。 */
    volatile uint32_t errors;                           /** UART累计通信错误次数。 */

    uint8_t image_frame[STM_IMAGE_FRAME_MAX_SIZE];      /** 发往STM的完整图片协议帧缓冲区。 */
    uint32_t image_length;                              /** 待发送图片协议帧总长度，单位字节。 */
    uint32_t image_offset;                              /** 图片协议帧当前已提交发送的偏移量。 */
    uint16_t jpeg_length;                               /** 当前图片协议帧中的JPEG长度。 */
    uint8_t image_id;                                   /** 当前待发送图片的帧ID。 */
    volatile bool image_waiting_tx_empty;               /** 图片分块是否正在等待UART发送完成。 */
    volatile bool image_tx_complete_due;                /** ISR是否已挂起图片分块发送完成事件。 */

    uint8_t short_frame[LEGACY_CONTROL_FRAME_MAX_SIZE]; /** 发往STM的短控制帧缓冲区。 */
    uint16_t short_length;                              /** 待发送短控制帧长度，单位字节。 */
    uint8_t short_command;                              /** 当前短控制帧的命令字。 */
    volatile bool short_waiting_tx_empty;               /** 短控制帧是否正在等待UART发送完成。 */
    volatile bool short_tx_complete_due;                /** ISR是否已挂起短控制帧发送完成事件。 */

    bool control_response_seen;                         /** 当前控制请求是否已收到首个有效响应。 */
    uint8_t expected_control_response;                  /** 当前控制请求期望的无线响应命令字。 */
    uint8_t pending_control[LEGACY_CONTROL_FRAME_MAX_SIZE]; /** 等待转发STM的控制响应帧。 */
    uint16_t pending_control_length;                    /** 待转发控制响应帧长度，单位字节。 */
    uint8_t pending_antenna_test_acks;                  /** 相同的天线测试回包待发次数。 */
    ReceiverControlStreamParser_t control_parser;       /** 跨UART静默数据块保存控制帧状态。 */
} receiver_uart_state_t;

static receiver_uart_state_t m_uart;

/** @brief 计算ZAYS控制帧载荷的8位累加校验和。 */
static uint8_t receiver_control_checksum(const uint8_t *data,
                                         uint32_t length)
{
    uint8_t sum = 0u;
    uint32_t i;
    for (i = 0u; i < length; ++i)
    {
        sum = (uint8_t)(sum + data[i]);
    }
    return sum;
}

/** @brief 封装一帧ZAYS控制协议并通过短帧DMA发送给STM。 */
static bool receiver_control_send_uart(uint8_t command,
                                       const uint8_t *payload,
                                       uint16_t payload_length)
{
    uint8_t frame[LEGACY_CONTROL_FRAME_MAX_SIZE] = {0};
    uint16_t frame_length = (uint16_t)(payload_length + 8u);

    if (frame_length > sizeof(frame))
    {
        return false;
    }
    /* 帧格式：ZAYS + 命令 + 大端载荷长度 + 载荷 + 累加校验。 */
    frame[0] = 0x5Au;
    frame[1] = 0x41u;
    frame[2] = 0x59u;
    frame[3] = 0x53u;
    frame[4] = command;
    LegacyProtocol_PutU16Be(&frame[5], payload_length);
    if ((payload != NULL) && (payload_length != 0u))
    {
        memcpy(&frame[7], payload, payload_length);
    }
    frame[7u + payload_length] =
        receiver_control_checksum(&frame[7], payload_length);
    return receiver_uart_send(frame, frame_length);
}

/** @brief 将STM请求命令映射为TX应答命令，0表示RX不转发该命令。 */
static uint8_t receiver_expected_response(uint8_t command)
{
    if (command == LEGACY_CMD_SN_PREPARE_REQUEST)
    {
        return LEGACY_CMD_SN_PREPARE_RESPONSE;
    }
    if (command == LEGACY_CMD_DEVICE_ID_QUERY_REQUEST)
    {
        return LEGACY_CMD_DEVICE_ID_QUERY_RESPONSE;
    }
    if (command == LEGACY_CMD_SN_SET_REQUEST)
    {
        return LEGACY_CMD_SN_SET_RESPONSE;
    }
    if (command == LEGACY_CMD_SN_CONFIRM_REQUEST)
    {
        return LEGACY_CMD_SN_CONFIRM_RESPONSE;
    }
    return 0u;
}

/** @brief 流式处理ZAYS控制帧，允许一帧跨多个UART静默数据块。 */
static void receiver_control_handle_uart(const uint8_t *data,
                                         uint32_t length)
{
    uint32_t index;

    for (index = 0u; index < length; ++index)
    {
        ReceiverControlStreamResult_t parse_result =
            ReceiverControlStream_Feed(&m_uart.control_parser, data[index]);
        const uint8_t *frame = m_uart.control_parser.frame;
        uint16_t payload_length;
        uint16_t frame_length;

        if (parse_result == RECEIVER_CONTROL_STREAM_MORE)
        {
            continue;
        }
        if (parse_result == RECEIVER_CONTROL_STREAM_INVALID_LENGTH)
        {
            CONTROL_LOG_WARNING("[CTRL] UART invalid control length");
            if (frame[4] != LEGACY_CMD_ANTENNA_TEST_REQUEST)
            {
                (void)receiver_control_send_uart(
                    LEGACY_CMD_CONTROL_ERROR_RESPONSE, NULL, 0u);
            }
            continue;
        }

        payload_length = (uint16_t)(m_uart.control_parser.expected - 8u);
        frame_length = m_uart.control_parser.expected;
        if (receiver_control_checksum(&frame[7], payload_length) !=
            frame[frame_length - 1u])
        {
            CONTROL_LOG_WARNING("[CTRL] UART invalid control checksum");
            if (frame[4] != LEGACY_CMD_ANTENNA_TEST_REQUEST)
            {
                (void)receiver_control_send_uart(
                    LEGACY_CMD_CONTROL_ERROR_RESPONSE, NULL, 0u);
            }
            ReceiverControlStream_Reset(&m_uart.control_parser);
            continue;
        }

        CONTROL_LOG_INFO("[CTRL] UART RX cmd=0x%02x payload=%u",
                         (unsigned)frame[4], (unsigned)payload_length);

        /* 查询、解绑和绑定由RX本地处理，不占用TX无线链路。 */
        if ((frame[4] == LEGACY_CMD_SN_QUERY_REQUEST) &&
            (payload_length == 0u))
        {
            /* 未绑定时返回全0 SN，保持查询响应始终携带固定8字节载荷。 */
            uint8_t empty_sn[LEGACY_CAPSULE_SN_SIZE] = {0};
            const uint8_t *sn = receiver_binding_is_bound() ?
                                receiver_binding_get() : empty_sn;
            CONTROL_LOG_INFO("[BIND] query bound=%u",
                             receiver_binding_is_bound() ? 1u : 0u);
            (void)receiver_control_send_uart(
                LEGACY_CMD_SN_QUERY_RESPONSE, sn, LEGACY_CAPSULE_SN_SIZE);
        }
        else if ((frame[4] == LEGACY_CMD_SN_UNBIND_REQUEST) &&
                 (payload_length == 0u))
        {
            /* 清除RAM绑定并通知天线状态机返回未绑定发现流程。 */
            uint8_t result = receiver_binding_clear() ?
                             LEGACY_CONTROL_RESULT_OK :
                             LEGACY_CONTROL_RESULT_ERROR;
            receiver_antenna_binding_changed();
            CONTROL_LOG_INFO("[BIND] unbind result=%u", (unsigned)result);
            (void)receiver_control_send_uart(
                LEGACY_CMD_SN_UNBIND_RESPONSE, &result, 1u);
        }
        else if ((frame[4] == LEGACY_CMD_SN_BIND_REQUEST) &&
                 (payload_length == LEGACY_CAPSULE_SN_SIZE))
        {
            /* frame[7]开始是8字节SN；绑定后天线状态机转入SEEK_END。 */
            uint8_t result = receiver_binding_set(&frame[7]) ?
                             LEGACY_CONTROL_RESULT_OK :
                             LEGACY_CONTROL_RESULT_ERROR;
            receiver_antenna_binding_changed();
            CONTROL_LOG_INFO("[BIND] bind result=%u SN:", (unsigned)result);
            NRF_LOG_HEXDUMP_INFO(&frame[7], LEGACY_CAPSULE_SN_SIZE);
            (void)receiver_control_send_uart(
                LEGACY_CMD_SN_BIND_RESPONSE, &result, 1u);
        }
        else if (frame[4] == LEGACY_CMD_ANTENNA_TEST_REQUEST)
        {
            /* payload仅有一个ID；00启动、01～0C选路、0D停止。 */
            if ((payload_length == 1u) &&
                receiver_antenna_manual_command(frame[7]))
            {
                /* 回包完全相同，计数排队可避免图片DMA繁忙时丢应答。 */
                if (m_uart.pending_antenna_test_acks < 0xFFu)
                {
                    ++m_uart.pending_antenna_test_acks;
                }
                else
                {
                    CONTROL_LOG_WARNING("[ANT TEST] response queue full");
                }
            }
            else
            {
                CONTROL_LOG_WARNING("[ANT TEST] invalid id or length");
            }
        }
        else
        {
            /* 出厂配置命令转发TX，并只接受对应命令号的首个应答。 */
            m_uart.control_response_seen = false;       // 为新请求开放首个无线应答
            m_uart.expected_control_response =
                receiver_expected_response(frame[4]);
            if (m_uart.expected_control_response == 0u)
            {
                /* 未定义请求/应答映射时不占用无线链路，直接向STM报错。 */
                (void)receiver_control_send_uart(
                    LEGACY_CMD_CONTROL_ERROR_RESPONSE, NULL, 0u);
            }
            else
            {
                /* 无线重复发送三次提高TX收到出厂配置命令的概率。 */
                CONTROL_LOG_INFO("[CTRL] Radio TX cmd=0x%02x repeat=3",
                                 (unsigned)frame[4]);
                (void)receiver_radio_send(frame, (uint16_t)frame_length, 3u);
            }
        }
        ReceiverControlStream_Reset(&m_uart.control_parser);
    }
}

/** @brief UART中断回调：搬运接收字节并把DMA完成转换为主循环事件。 */
/*
 * 中断分流：
 *
 *                         UART事件
 *                            │
 *       ┌────────────────────┼─────────────────────┐
 *       ▼                    ▼                     ▼
 *  DATA_READY        COMM/FIFO_ERROR           TX_EMPTY
 *       │                    │                     │
 *       ▼                    ▼                     ▼
 *  读空驱动RX FIFO       累计错误/丢包       判断完成的是哪类DMA
 *       │                                          │
 *       ▼                              ┌───────────┴───────────┐
 *  写入软件ring[]                       ▼                       ▼
 *  并刷新分包状态              image_waiting=true      short_waiting=true
 *       │                              │                       │
 *       ▼                              ▼                       ▼
 *  协议解析留给主循环          image_complete_due=true short_complete_due=true
 *
 * ISR只搬字节、计数和置事件标志，不在中断中解析协议或启动下一段DMA。
 */
static void receiver_uart_event_handler(app_uart_evt_t *event)
{
    if (event->evt_type == APP_UART_DATA_READY)
    {
        uint8_t byte;
        /* ISR只写环形缓冲，协议查找和业务处理留给主循环。 */
        while (app_uart_get(&byte) == NRF_SUCCESS)
        {
            uint16_t tail = m_uart.ring_tail;           /** 本字节写入位置。 */
            uint16_t next = (uint16_t)((tail + 1u) & UART_RX_BUFFER_MASK);
            ++m_uart.received_bytes;                    // 统计驱动实际交付的全部字节
            m_uart.idle_timeout = false;                // 新字节到达，原静默超时失效
            m_uart.packet_active = true;                // 标记正在组装一个UART静默数据块
            if (next != m_uart.ring_head)
            {
                m_uart.ring[tail] = byte;               // 先写数据，再发布新的tail
                m_uart.ring_tail = next;
            }
            else
            {
                ++m_uart.dropped_bytes;                 // 保留一个空槽区分队列空和满
            }
        }
    }
    else if (event->evt_type == APP_UART_COMMUNICATION_ERROR)
    {
        ++m_uart.errors;                                // 记录奇偶、帧、溢出等硬件通信错误
        CONTROL_LOG_WARNING("[UART] communication error=0x%08x total=%u",
                            (unsigned)event->data.error_communication,
                            (unsigned)m_uart.errors);
    }
    else if (event->evt_type == APP_UART_FIFO_ERROR)
    {
        ++m_uart.dropped_bytes;                         // SDK FIFO无法接纳数据，按丢字节统计
        CONTROL_LOG_WARNING("[UART] FIFO error=%u dropped=%u",
                            (unsigned)event->data.error_code,
                            (unsigned)m_uart.dropped_bytes);
    }
    else if (event->evt_type == APP_UART_TX_EMPTY)
    {
        /* 这里只置完成标志，不能在中断中启动下一段DMA。 */
        if (m_uart.image_waiting_tx_empty)
        {
            m_uart.image_tx_complete_due = true;        // 主循环回收图片块并启动下一块
        }
        else if (m_uart.short_waiting_tx_empty)
        {
            m_uart.short_tx_complete_due = true;        // 主循环回收短控制帧状态
        }
    }
}

/** @brief 启动16 MHz高频晶振，为Radio、UART和TIMER提供稳定时钟。 */
void receiver_clock_init(void)
{
    NRF_CLOCK->EVENTS_HFCLKSTARTED = 0u;
    NRF_CLOCK->TASKS_HFCLKSTART = 1u;
    while (NRF_CLOCK->EVENTS_HFCLKSTARTED == 0u) {}
}

/** @brief 初始化外部收发模式脚，默认保持接收方向。 */
void receiver_mode_pin_init(void)
{
    nrf_gpio_cfg_output(RECEIVER_MODE_PIN);
    nrf_gpio_pin_clear(RECEIVER_MODE_PIN);
}

/** @brief 用PPI把每次RXDRDY时间自动捕获到TIMER1 CC[1]。 */
void receiver_uart_idle_capture_init(void)
{
    /* TIMER1保持自由运行，CC[1]只保存最后一个物理字节的绝对时刻。 */
    NRF_TIMER1->TASKS_CAPTURE[1] = 1u;
    NRF_PPI->CHENCLR = (1u << UART_IDLE_PPI_RX_CHANNEL);
    NRF_PPI->CH[UART_IDLE_PPI_RX_CHANNEL].EEP =
        (uint32_t)&NRF_UARTE0->EVENTS_RXDRDY;
    NRF_PPI->CH[UART_IDLE_PPI_RX_CHANNEL].TEP =
        (uint32_t)&NRF_TIMER1->TASKS_CAPTURE[1];
    NRF_PPI->FORK[UART_IDLE_PPI_RX_CHANNEL].TEP = 0u;
    NRF_PPI->CHENSET = (1u << UART_IDLE_PPI_RX_CHANNEL);
}

/** @brief 检测UART静默间隔，到期后停止当前RX DMA形成一个软件数据包。 */
void receiver_uart_idle_tick_1ms(uint32_t now_us)
{
    uint32_t dma_amount = NRF_UARTE0->RXD.AMOUNT;
    uint32_t last_byte_us = NRF_TIMER1->CC[1];

    if ((!m_uart.packet_active && (dma_amount == 0u)) ||
        m_uart.idle_timeout ||
        ((uint32_t)(now_us - last_byte_us) < UART_RX_IDLE_TIMEOUT_US))
    {
        return;
    }
    /* STOPRX触发驱动交付最后一个不足DMA块的数据；随后由驱动恢复接收。 */
    if (dma_amount != 0u)
    {
        NRF_UARTE0->SHORTS &= ~UARTE_SHORTS_ENDRX_STARTRX_Msk;
        NRF_UARTE0->TASKS_STOPRX = 1u;
    }
    m_uart.idle_timeout = true;
}

/** @brief 初始化RX与STM之间的1 Mbps UARTE及软件收发缓冲。 */
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

    memset(&m_uart, 0, sizeof(m_uart));
    APP_UART_FIFO_INIT(&params, UART_RX_BUFFER_SIZE, UART_TX_FIFO_SIZE,
                       receiver_uart_event_handler,
                       (app_irq_priority_t)UART_IRQ_PRIORITY, error);
    APP_ERROR_CHECK(error);
    NRF_LOG_INFO("[UART] ready 1Mbps RX=P0.%u TX=P0.%u",
                 (unsigned)UART_RX_PIN, (unsigned)UART_TX_PIN);
}

/** @brief 主循环搬空RX环形缓冲，并在静默到期后解析一个数据块。 */
/* 
阶段一：搬数据
UART RX环形缓冲区 → packet连续缓冲区

阶段二：判定一包结束
连续100 ms没有新字节 → 调用协议解析函数
*/
bool receiver_uart_rx_service(void)
{
	/*
	worked
	表示本轮是否完成了工作，例如：
	- 从环形缓冲区搬走了字节。
	- 完成并解析了一个数据块。
	主循环根据这个返回值判断是否还有任务在运行。
	complete
	表示本轮是否确认一个UART静默数据块已经结束，可以开始解析。
	head
	把共享的读指针复制到局部变量，搬运期间先使用局部变量前进，最后一次性更新：
	*/
    bool worked = false;
    bool complete = false;
    uint16_t head = m_uart.ring_head;

    /* 先复制ISR写入的全部字节，再更新共享读指针。 */
    while (head != m_uart.ring_tail)
    {
        uint8_t byte = m_uart.ring[head];
        head = (uint16_t)((head + 1u) & UART_RX_BUFFER_MASK);
        if (m_uart.packet_length < sizeof(m_uart.packet))
        {
            m_uart.packet[m_uart.packet_length++] = byte;
        }
        else
        {
            ++m_uart.packet_overflow;
        }
        worked = true;
    }
    m_uart.ring_head = head;

    /* 与ISR二次确认队列已空，避免在新字节到达时提前结束本包。 */
    if (m_uart.packet_active && m_uart.idle_timeout)
    {
        __disable_irq();
        if (m_uart.packet_active && m_uart.idle_timeout &&
            (m_uart.ring_head == m_uart.ring_tail))
        {
            m_uart.packet_active = false;
            m_uart.idle_timeout = false;
            complete = true;
        }
        __enable_irq();
    }

    if (complete)
    {
        CONTROL_LOG_INFO("[UART] RX packet bytes=%u overflow=%u",
                         (unsigned)m_uart.packet_length,
                         (unsigned)m_uart.packet_overflow);
        receiver_control_handle_uart(m_uart.packet, m_uart.packet_length);
        m_uart.packet_length = 0u;
        m_uart.packet_overflow = 0u;
        worked = true;
    }
    return worked;
}

/** @brief 处理无线侧扫描通知、SN广播和TX控制应答。 */
/* 
收到无线包
    │
    ├── packet == NULL
    │       └── 返回false
    │
    ├── 命令0x11
    │       ├── 校验绑定SN
    │       ├── 挂起FAST_SCAN事件
    │       └── 返回true
    │
    ├── 命令0x05
    │       ├── 未绑定：SN上报STM
    │       ├── 已绑定：只刷新匹配SN
    │       └── 返回true
    │
    ├── 不是ZAYS
    │       └── 返回false，交给图片模块
    │
    └── 是ZAYS
            ├── 检查载荷长度
            ├── 检查校验和
            ├── 检查响应命令
            ├── 过滤重复响应
            ├── UART空闲：立即发送STM
            ├── UART繁忙：暂存等待
            └── 返回true
*/
bool receiver_control_handle_radio_packet(const uint8_t *packet)
{
    uint16_t payload_length;
    uint16_t frame_length;

    if (packet == NULL)
    {
        return false;
    }
    /* 0x11是快速广播许可，交给天线状态机校验SN并切换状态。 */
    if (packet[0] == LEGACY_CMD_FAST_SCAN_START)
    {
        receiver_antenna_fast_scan_granted(&packet[1]);
        return true;
    }
    if (packet[0] == LEGACY_CMD_CAPSULE_SN_BROADCAST)
    {
        /* 未绑定时把0x05封装为ZAYS短帧上报STM；绑定后只刷新SN缓存。 */
        if (!receiver_binding_is_bound())
        {
            CONTROL_LOG_INFO("[DISCOVERY] SN received on ANT%u:",
                             (unsigned)(rf1662_get_antenna() + 1u));
            NRF_LOG_HEXDUMP_INFO(&packet[1], LEGACY_CAPSULE_SN_SIZE);
            receiver_antenna_note_discovery_sn();				//发现SN后，暂时不立即切到下一路天线。
            receiver_image_update_capsule_sn(&packet[1]);		//更新图片模块的SN缓存
            if (receiver_uart_image_tx_busy())
            {
				//如果UART正在向STM发送一整幅图片，当前实现不打断图片DMA发送。
                CONTROL_LOG_WARNING("[DISCOVERY] SN not sent: image UART busy");
            }
			//无线侧的0x05包不能直接原样发给STM，需要封装成ZAYS控制帧。
            else if (receiver_control_send_uart(
                         LEGACY_CMD_CAPSULE_SN_BROADCAST,
                         &packet[1], LEGACY_CAPSULE_SN_SIZE))
            {
                CONTROL_LOG_INFO("[DISCOVERY] SN sent to STM cmd=0x05 bytes=16");
            }
            else
            {
                CONTROL_LOG_WARNING("[DISCOVERY] SN UART send failed");
            }
            return true;
        }

        /* 绑定后广播只用于刷新当前胶囊信息，不重复上报STM。 */
        if (receiver_binding_matches(&packet[1]))
        {
            receiver_image_update_capsule_sn(&packet[1]);
        }
        return true;
    }

    /* 其余包只有以ZAYS开头时才属于控制应答。 */
    if ((packet[0] != 0x5Au) || (packet[1] != 0x41u) ||
        (packet[2] != 0x59u) || (packet[3] != 0x53u))
    {
        return false;
    }

    payload_length = LegacyProtocol_GetU16Be(&packet[5]);
    if (payload_length > (LEGACY_CONTROL_FRAME_MAX_SIZE - 8u))
    {
		//格式错误的ZAYS帧
		//属于控制类数据，只是内容非法；不能再把它当作图片包处理。
        return true;
    }
    frame_length = (uint16_t)(payload_length + 8u);
    if ((frame_length > LEGACY_CONTROL_FRAME_MAX_SIZE) ||
        (receiver_control_checksum(&packet[7], payload_length) !=
         packet[frame_length - 1u]))
    {
        CONTROL_LOG_WARNING("[CTRL] Radio invalid response");
        return true;
    }
    /* 命令号必须与当前请求匹配；三次无线重发只向STM交付一次。 */
    if ((m_uart.expected_control_response != 0u) &&
        (packet[4] != m_uart.expected_control_response) &&
        (packet[4] != LEGACY_CMD_CONTROL_ERROR_RESPONSE))
    {
        CONTROL_LOG_WARNING("[CTRL] response mismatch expected=0x%02x got=0x%02x",
                            (unsigned)m_uart.expected_control_response,
                            (unsigned)packet[4]);
        return true;
    }
	
	/*
		发送新控制请求时会先设置：
		m_uart.control_response_seen = false;
		收到第一个合法响应后设置为：true
		后面相同的无线重发就全部忽略。
	*/
    if (m_uart.control_response_seen)
    {
        CONTROL_LOG_INFO("[CTRL] duplicate response cmd=0x%02x ignored",
                         (unsigned)packet[4]);
        return true;
    }
    m_uart.control_response_seen = true;
	
    /* 图片DMA不可打断，控制应答暂存一帧，待图片发送完成后补发。 */
    if (receiver_uart_image_tx_busy())
    {
        memcpy(m_uart.pending_control, packet, frame_length);
        m_uart.pending_control_length = frame_length;
        CONTROL_LOG_INFO("[CTRL] response cmd=0x%02x queued behind image",
                         (unsigned)packet[4]);
    }
    else
    {
        CONTROL_LOG_INFO("[CTRL] response cmd=0x%02x sent to STM",
                         (unsigned)packet[4]);
        (void)receiver_uart_send(packet, frame_length);
    }
    return true;
}

/** @brief 回收短帧DMA状态，并在UART空闲时补发排队的控制应答。 */
/*
短控制帧DMA完成了吗？
    ├─ 是 → 清理短帧发送状态
    │
    └─ 否
        ↓
有暂存控制响应吗？
    ├─ 否 → 返回
    │
    └─ 是
        ↓
UART是否还在发送图片或短帧？
    ├─ 是 → 继续等待
    │
    └─ 否 → 启动控制响应DMA发送
*/
bool receiver_uart_control_service(void)
{
    static const uint8_t antenna_test_reply[8] =
        {0x5Au, 0x41u, 0x59u, 0x53u,
         LEGACY_CMD_ANTENNA_TEST_RESPONSE, 0x00u, 0x00u, 0x00u};

	//短帧DMA完成状态
    if (m_uart.short_tx_complete_due)
    {
		/*
			short_waiting_tx_empty：DMA已启动，正在等待完成。
			short_tx_complete_due：DMA已经完成，等待主循环清理状态。
		*/
        m_uart.short_tx_complete_due = false;
        m_uart.short_waiting_tx_empty = false;
        CONTROL_LOG_INFO("[UART] short frame TX complete cmd=0x%02x bytes=%u",
                         (unsigned)m_uart.short_command,
                         (unsigned)m_uart.short_length);
        m_uart.short_length = 0u;
        return true;
    }

    if ((m_uart.short_length != 0u) || receiver_uart_image_tx_busy())
    {
        return false;
    }

    if (m_uart.pending_antenna_test_acks != 0u)
    {
        if (receiver_uart_send(antenna_test_reply, sizeof(antenna_test_reply)))
        {
            --m_uart.pending_antenna_test_acks;
            return true;
        }
        return false;
    }

	//检查排队的控制响应
    if (m_uart.pending_control_length == 0u)
    {
        return false;
    }

	//UART空闲后补发
    if (receiver_uart_send(m_uart.pending_control,
                           m_uart.pending_control_length))
    {
        CONTROL_LOG_INFO("[CTRL] queued response cmd=0x%02x sent",
                         (unsigned)m_uart.pending_control[4]);
        m_uart.pending_control_length = 0u;
        return true;
    }
    return false;
}

/** @brief 启动一帧连续短数据EasyDMA发送，缓冲保持到TX_EMPTY。 */
bool receiver_uart_send(const uint8_t *data, uint32_t length)
{
    uint32_t result;

    if ((data == NULL) || (length == 0u) ||
        (length > sizeof(m_uart.short_frame)) ||
        (m_uart.short_length != 0u) ||
        (m_uart.image_length != 0u))
    {
        return false;
    }

    /* 必须复制到模块静态RAM；EasyDMA完成前调用者缓冲可能已失效。 */
    memcpy(m_uart.short_frame, data, length);
    m_uart.short_length = (uint16_t)length;
    m_uart.short_command = (length > 4u) ? data[4] : 0u;

    /* 启动DMA和设置等待标志必须原子完成，防止TX_EMPTY先到。 */
    __disable_irq();
    result = app_uart_tx_buffer(m_uart.short_frame,
                                m_uart.short_length);
    if (result == NRF_SUCCESS)
    {
        m_uart.short_waiting_tx_empty = true;
    }
    __enable_irq();

    if (result != NRF_SUCCESS)
    {
        NRF_LOG_ERROR("[UART] short DMA start failed cmd=0x%02x error=%u",
                      (unsigned)m_uart.short_command, (unsigned)result);
        m_uart.short_length = 0u;
        return false;
    }
    return true;
}

/** @brief 组装FF551234图片帧并排队，实际DMA发送由服务函数推进。 */
bool receiver_uart_queue_image(const uint8_t *device_info,
                               const uint8_t *jpeg,
                               uint16_t jpeg_length,
                               uint8_t image_id)
{
    uint16_t payload_length;
    uint32_t frame_length;
    uint16_t i;
    uint8_t checksum = 0u;

    if ((device_info == NULL) || (jpeg == NULL) ||
        (jpeg_length == 0u) || (jpeg_length > LEGACY_IMAGE_MAX_SIZE) ||
        receiver_uart_image_tx_busy())
    {
        NRF_LOG_WARNING("[UART] reject image queue busy=%u jpeg=%u",
                        receiver_uart_image_tx_busy() ? 1u : 0u,
                        (unsigned)jpeg_length);
        return false;
    }

    /* 载荷固定为128字节设备信息，随后紧跟本帧JPEG。 */
    payload_length = (uint16_t)(LEGACY_DEVICE_INFO_SIZE + jpeg_length);
    frame_length = LEGACY_STM_FRAME_HEADER_SIZE + payload_length + 1u;
    memset(m_uart.image_frame, 0, frame_length);
    m_uart.image_frame[0] = 0xFFu;
    m_uart.image_frame[1] = 0x55u;
    m_uart.image_frame[2] = 0x12u;
    m_uart.image_frame[3] = 0x34u;
    m_uart.image_frame[4] = LEGACY_CMD_IMAGE_FORWARD;
    LegacyProtocol_PutU16Be(&m_uart.image_frame[5], payload_length);
    memcpy(&m_uart.image_frame[LEGACY_STM_FRAME_HEADER_SIZE],
           device_info, LEGACY_DEVICE_INFO_SIZE);
    memcpy(&m_uart.image_frame[LEGACY_STM_FRAME_HEADER_SIZE +
                               LEGACY_DEVICE_INFO_SIZE],
           jpeg, jpeg_length);
    for (i = 0u; i < payload_length; ++i)
    {
        checksum = (uint8_t)(checksum +
            m_uart.image_frame[LEGACY_STM_FRAME_HEADER_SIZE + i]);
    }
    m_uart.image_frame[frame_length - 1u] = checksum;
    m_uart.image_length = frame_length;
    m_uart.image_offset = 0u;
    m_uart.jpeg_length = jpeg_length;
    m_uart.image_id = image_id;
    return true;
}

/** @brief 判断图片或短控制帧是否占用唯一UART发送通道。 */
bool receiver_uart_image_tx_busy(void)
{
    return (m_uart.image_length != 0u) || (m_uart.short_length != 0u);
}

/** @brief 非阻塞推进图片UART发送，每轮最多启动一个DMA块。 */
/* 图片排队
image_length > 0
image_offset = 0
        ↓
主循环启动第1块DMA
        ↓
image_waiting_tx_empty = true
        ↓
UART发送完成中断
        ↓
image_tx_complete_due = true
        ↓
主循环确认完成
        ↓
还有数据？
   ├─ 是 → 启动下一块
   └─ 否 → 整幅图片发送完成，释放状态
*/
bool receiver_uart_tx_service(void)
{
    uint32_t remaining;
    uint16_t chunk;
    uint32_t result;

    /* TX_EMPTY表示上一块已经发完；最后一块完成时关闭整帧事务。 */
    if (m_uart.image_tx_complete_due)
    {
		//上一块已经完成
		//当前不再等待TX_EMPTY
        m_uart.image_tx_complete_due = false;
        m_uart.image_waiting_tx_empty = false;
        if ((m_uart.image_length != 0u) &&
            (m_uart.image_offset == m_uart.image_length))
        {
            receiver_image_note_stm_forwarded(m_uart.image_id,
                                              m_uart.jpeg_length);
            m_uart.image_length = 0u;
            m_uart.image_offset = 0u;
            return true;
        }
    }

	//正在等待上一块完成          m_uart.image_waiting_tx_empty  =1
	//当前没有排队图片 image_length 
    if (m_uart.image_waiting_tx_empty || (m_uart.image_length == 0u))
    {
        return false;
    }

    /* 大图片按固定上限分块，避免驱动FIFO和EasyDMA长度限制。 */
    remaining = m_uart.image_length - m_uart.image_offset;
    chunk = (remaining > UART_TX_DMA_CHUNK_SIZE) ?
            (uint16_t)UART_TX_DMA_CHUNK_SIZE : (uint16_t)remaining;
	/*
	关闭中断的目的是让以下操作成为一个不可分割的整体：
	启动DMA
	更新image_offset
	设置image_waiting_tx_empty
	*/
    __disable_irq();
    result = app_uart_tx_buffer(&m_uart.image_frame[m_uart.image_offset],
                                chunk);
    if (result == NRF_SUCCESS)
    {
        m_uart.image_offset += chunk;
        m_uart.image_waiting_tx_empty = true;
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
    NRF_LOG_ERROR("[UART] image DMA failed offset=%u/%u error=%u",
                  (unsigned)m_uart.image_offset,
                  (unsigned)m_uart.image_length,
                  (unsigned)result);
    m_uart.image_length = 0u;
    m_uart.image_offset = 0u;
    return true;
}
