/**
 * @file antenna_manager.c
 * @brief 12路天线业务状态机。
 *
 * 状态顺序：DISCOVERY -> SEEK_END -> FAST_SCAN -> LOCKED。
 */

#include "antenna_manager.h"
#include "binding_storage.h"
#include "image.h"
#include "legacy_protocol.h"
#include "nrf.h"
#include "nrf_log.h"
#include "radio_link.h"
#include "receiver_timebase.h"
#include "rf1662.h"
#include <string.h>

typedef enum
{
    ANTENNA_STATE_DISCOVERY = 0, /** 未绑定，逐路寻找SN。 */
    ANTENNA_STATE_SEEK_END,      /** 已绑定，寻找END并发送0x12。 */
    ANTENNA_STATE_FAST_SCAN,     /** TX快速广播期间正式统计RSSI。 */
    ANTENNA_STATE_LOCKED         /** 锁定候选天线接收图片。 */
} receiver_antenna_state_id_t;

typedef struct
{
    receiver_antenna_state_id_t state;                  /** 当前天线业务状态。 */

    volatile uint32_t service_elapsed_ms;               /** 周期服务累计时间，单位ms。 */
    volatile bool service_due;                          /** 已到达一次天线调度周期。 */

    uint8_t antenna;                                    /** 当前正在扫描的天线索引。 */
    uint8_t round;                                      /** 当前扫描已完成的轮数。 */
    uint32_t deadline_ms;                               /** 当前天线驻留截止时间，单位ms。 */
    uint32_t hold_until_ms;                             /** 协商期间保持当前天线的截止时间，单位ms。 */
    bool discovery_holding;                             /** 发现SN后是否正在延长当前天线驻留。 */

    volatile bool end_pending;                          /** ISR已捕获待处理的图片END包。 */
    volatile uint8_t end_frame_id;                      /** 待处理END包对应的图片帧ID。 */
    volatile bool fast_start_pending;                   /** ISR已收到待处理的快速扫描许可。 */
    volatile bool request_pending;                      /** 当前是否允许捕获END并发起快速扫描请求。 */
    volatile uint8_t request_attempts;                  /** 本轮快速扫描请求已发送次数。 */

    volatile uint32_t window_total;                     /** 当前驻留窗口收到的无线包总数。 */
    volatile uint32_t window_valid;                     /** 当前窗口中SN匹配的有效广播包数。 */
    volatile uint32_t window_rssi_sum;                  /** 当前窗口有效广播包的RSSI幅值累计和。 */
    uint32_t valid_count[RF1662_ANTENNA_COUNT];         /** 各天线累计的有效RSSI样本数。 */
    uint32_t rssi_sum[RF1662_ANTENNA_COUNT];            /** 各天线累计的RSSI幅值总和。 */
    uint8_t latest_rssi[RF1662_ANTENNA_COUNT];          /** 各天线最近统计的平均RSSI幅值。 */

    uint8_t candidates[3];                              /** 按信号质量排序的前三个天线索引。 */
    uint8_t candidate_count;                            /** 当前有效候选天线数量。 */
    uint8_t candidate_index;                            /** 当前锁定天线在候选数组中的位置。 */

    volatile uint32_t last_target_packet_ms;            /** 最近收到绑定胶囊有效包的时间，单位ms。 */
    uint32_t last_complete_image_ms;                    /** 最近完整接收一帧图片的时间，单位ms。 */
    uint8_t failed_frame_count;                         /** 当前锁定天线连续接收失败的图片帧数。 */
} receiver_antenna_state_t;

static receiver_antenna_state_t m_antenna;

/** @brief 按包类型找到SN字段，并判断是否属于当前绑定胶囊。 */
static bool receiver_packet_matches_bound_sn(const uint8_t *packet)
{
    if (!receiver_binding_is_bound())
    {
        return false;
    }
    /* 0x05/0x11的SN从下标1开始，图片0x01/0x02/0x03从下标2开始。 */
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

/** @brief 清空当前天线驻留窗口的临时包数和RSSI累加值。 */
static void receiver_antenna_reset_window(void)
{
    m_antenna.window_total = 0u;
    m_antenna.window_valid = 0u;
    m_antenna.window_rssi_sum = 0u;
}

/** @brief 进入未绑定发现状态，从默认天线逐路寻找任意SN广播。 */
static void receiver_antenna_start_discovery(void)
{
    m_antenna.state = ANTENNA_STATE_DISCOVERY;
    m_antenna.antenna = RF1662_DEFAULT_ANTENNA;
    m_antenna.discovery_holding = false;
    receiver_image_reset();
    receiver_radio_clear_queue();
    receiver_radio_select_antenna(m_antenna.antenna);
    m_antenna.deadline_ms = receiver_timebase_now_ms() +
                            RF1662_DISCOVERY_DWELL_MS;
    NRF_LOG_INFO("RF1662 state=DISCOVERY ANT%u",
                 (unsigned)(m_antenna.antenna + 1u));
}

/** @brief 进入协商准备状态，轮询12路寻找绑定胶囊的图片END。 */
static void receiver_antenna_start_seek_end(void)
{
    m_antenna.state = ANTENNA_STATE_SEEK_END;
    m_antenna.antenna = 0u;
    m_antenna.round = 0u;
    m_antenna.end_pending = false;
    m_antenna.hold_until_ms = 0u;
    receiver_antenna_reset_window();
    receiver_image_reset();
    receiver_radio_clear_queue();
    receiver_radio_select_antenna(m_antenna.antenna);
    m_antenna.deadline_ms = receiver_timebase_now_ms() +
                            RF1662_SEEK_END_DWELL_MS;
    NRF_LOG_INFO("RF1662 state=SEEK_END dwell=%ums rounds=%u",
                 (unsigned)RF1662_SEEK_END_DWELL_MS,
                 (unsigned)RF1662_SEEK_END_ROUNDS);
}

/** @brief 收到TX的0x11许可后，清空旧统计并开始正式RSSI扫描。 */
static void receiver_antenna_start_fast_scan(void)
{
    m_antenna.state = ANTENNA_STATE_FAST_SCAN;
    m_antenna.antenna = 0u;
    m_antenna.round = 0u;
    m_antenna.hold_until_ms = 0u;
    /* 每次正式扫描独立统计，避免把胶囊上次位置的数据带入本轮。 */
    memset(m_antenna.valid_count, 0, sizeof(m_antenna.valid_count));
    memset(m_antenna.rssi_sum, 0, sizeof(m_antenna.rssi_sum));
    memset(m_antenna.candidates, 0, sizeof(m_antenna.candidates));
    m_antenna.candidate_count = 0u;
    m_antenna.candidate_index = 0u;
    receiver_antenna_reset_window();
    receiver_image_reset();
    receiver_radio_clear_queue();
    receiver_radio_select_antenna(m_antenna.antenna);
    m_antenna.deadline_ms = receiver_timebase_now_ms() +
                            RF1662_FAST_SCAN_DWELL_MS;
    NRF_LOG_INFO("RF1662 state=FAST_SCAN dwell=%ums rounds=%u",
                 (unsigned)RF1662_FAST_SCAN_DWELL_MS,
                 (unsigned)RF1662_FAST_SCAN_ROUNDS);
}

/** @brief 比较两路扫描结果：样本可靠性优先，其次RSSI，再次样本数。 */
static bool receiver_candidate_is_better(uint8_t antenna, uint8_t old)
{
    uint32_t count = m_antenna.valid_count[antenna];
    uint32_t old_count = m_antenna.valid_count[old];
    uint32_t average = m_antenna.rssi_sum[antenna] / count;
    uint32_t old_average = m_antenna.rssi_sum[old] / old_count;
    bool reliable = count >= RF1662_MIN_RSSI_SAMPLES;
    bool old_reliable = old_count >= RF1662_MIN_RSSI_SAMPLES;

    if (reliable != old_reliable)
    {
        return reliable;
    }
    if (average != old_average)
    {
        return average < old_average;                    // RSSI幅值越小越强
    }
    return count > old_count;
}

/** @brief 计算12路平均RSSI，并按质量插入排序保留前三路。 */
static void receiver_antenna_build_candidates(void)
{
    uint8_t antenna;
    uint8_t position;

    m_antenna.candidate_count = 0u;
    /* RSSISAMPLE保存正的衰减幅值，例如30代表-30 dBm。 */
    for (antenna = 0u; antenna < RF1662_ANTENNA_COUNT; ++antenna)
    {
        m_antenna.latest_rssi[antenna] =
            (m_antenna.valid_count[antenna] != 0u) ?
            (uint8_t)(m_antenna.rssi_sum[antenna] /
                      m_antenna.valid_count[antenna]) : 0u;
        if (m_antenna.valid_count[antenna] != 0u)
        {
            NRF_LOG_INFO("[SCAN] ANT%u samples=%u avg_rssi=-%u dBm",
                         (unsigned)(antenna + 1u),
                         (unsigned)m_antenna.valid_count[antenna],
                         (unsigned)m_antenna.latest_rssi[antenna]);
        }
        else
        {
            NRF_LOG_INFO("[SCAN] ANT%u samples=0 no_data",
                         (unsigned)(antenna + 1u));
        }
    }

    /* 将每一路插入最多3项的有序候选数组。 */
    for (antenna = 0u; antenna < RF1662_ANTENNA_COUNT; ++antenna)
    {
        if (m_antenna.valid_count[antenna] == 0u)
        {
            continue;
        }
        for (position = 0u; position < m_antenna.candidate_count; ++position)
        {
            if (receiver_candidate_is_better(
                    antenna, m_antenna.candidates[position]))
            {
                break;
            }
        }
        if (position < 3u)
        {
            uint8_t move = (m_antenna.candidate_count < 3u) ?
                           m_antenna.candidate_count : 2u;
            while (move > position)
            {
                m_antenna.candidates[move] = m_antenna.candidates[move - 1u];
                --move;
            }
            m_antenna.candidates[position] = antenna;
            if (m_antenna.candidate_count < 3u)
            {
                ++m_antenna.candidate_count;
            }
        }
    }

    for (position = 0u; position < m_antenna.candidate_count; ++position)
    {
        antenna = m_antenna.candidates[position];
        NRF_LOG_INFO("[SCAN] candidate%u=ANT%u samples=%u rssi=-%u dBm",
                     (unsigned)(position + 1u), (unsigned)(antenna + 1u),
                     (unsigned)m_antenna.valid_count[antenna],
                     (unsigned)m_antenna.latest_rssi[antenna]);
    }
}

/** @brief 锁定指定候选天线，并重新开始链路健康监测。 */
static void receiver_antenna_lock(uint8_t candidate_index)
{
    uint8_t antenna = m_antenna.candidates[candidate_index];
    uint32_t now = receiver_timebase_now_ms();

    m_antenna.state = ANTENNA_STATE_LOCKED;
    m_antenna.candidate_index = candidate_index;
    m_antenna.request_pending = false;
    receiver_image_reset();
    receiver_radio_clear_queue();
    receiver_radio_select_antenna(antenna);
    m_antenna.last_target_packet_ms = now;
    m_antenna.last_complete_image_ms = now;
    m_antenna.failed_frame_count = 0u;
    NRF_LOG_INFO("RF1662 state=LOCKED candidate=%u/%u ANT%u rssi=-%u dBm",
                 (unsigned)(candidate_index + 1u),
                 (unsigned)m_antenna.candidate_count,
                 (unsigned)(antenna + 1u),
                 (unsigned)m_antenna.latest_rssi[antenna]);
}

/** @brief 前进到下一天线；ANT12之后回到ANT1并增加轮数。 */
static void receiver_antenna_advance(void)
{
    ++m_antenna.antenna;
    if (m_antenna.antenna >= RF1662_ANTENNA_COUNT)
    {
        m_antenna.antenna = 0u;
        ++m_antenna.round;
    }
}

/** @brief 发送0x12，请求TX暂停图片并进入快速SN广播窗口。 */
static void receiver_antenna_send_fast_scan_request(uint8_t image_id,
                                                     const uint8_t *sn)
{
    uint8_t request[RADIO_PACKET_SIZE] __ALIGNED(4) = {0};
    request[0] = LEGACY_CMD_FAST_SCAN_REQUEST;
    request[1] = image_id;
    memcpy(&request[2], sn, LEGACY_CAPSULE_SN_SIZE);
    ++m_antenna.request_attempts;
    (void)receiver_radio_send(request, RADIO_PACKET_SIZE, 2u);
    NRF_LOG_INFO("[SCAN] request frame=%u attempt=%u",
                 (unsigned)image_id,
                 (unsigned)m_antenna.request_attempts);
}

/** @brief 清空天线模块状态并从未绑定发现模式启动。 */
void receiver_antenna_init(void)
{
    memset(&m_antenna, 0, sizeof(m_antenna));
    receiver_antenna_start_discovery();
}

/** @brief 由统一时间基调用，仅产生周期服务标志，不在中断中切天线。 */
void receiver_antenna_tick_1ms(uint32_t elapsed_ms)
{
    m_antenna.service_elapsed_ms += elapsed_ms;
    if (m_antenna.service_elapsed_ms >= RF1662_SERVICE_TICK_MS)
    {
        m_antenna.service_elapsed_ms %= RF1662_SERVICE_TICK_MS;
        m_antenna.service_due = true;
    }
}

/** @brief Radio中断入口：更新链路时间，并消费扫描阶段的无线包。 */
bool receiver_antenna_on_radio_packet(const uint8_t *packet,
                                      bool crc_ok, uint8_t rssi)
{
    bool target_packet = crc_ok && receiver_packet_matches_bound_sn(packet);

    /* 任何CRC正确且SN匹配的包，都证明当前绑定胶囊仍在线。 */
    if (target_packet)
    {
        m_antenna.last_target_packet_ms = receiver_timebase_now_ms();
        if (m_antenna.state == ANTENNA_STATE_LOCKED)
        {
            uint8_t antenna = rf1662_get_antenna();
            if (antenna < RF1662_ANTENNA_COUNT)
            {
                m_antenna.latest_rssi[antenna] = rssi;
            }
        }
    }

    /* SEEK_END只等待END或0x11，不重组图片，也不把RSSI计入正式结果。 */
    if (m_antenna.state == ANTENNA_STATE_SEEK_END)
    {
        if (m_antenna.request_pending && target_packet &&
            (packet[0] == LEGACY_CMD_IMAGE_END))
        {
            m_antenna.end_frame_id = packet[1];
            m_antenna.end_pending = true;
        }
        if (m_antenna.request_pending && target_packet &&
            (packet[0] == LEGACY_CMD_FAST_SCAN_START))
        {
            m_antenna.request_pending = false;
            m_antenna.request_attempts = 0u;
            m_antenna.fast_start_pending = true;
        }
        return true;                                    // 寻找阶段不重组图片
    }

    /* 正式扫描只统计CRC正确、SN匹配的0x05广播包。 */
    if (m_antenna.state == ANTENNA_STATE_FAST_SCAN)
    {
        ++m_antenna.window_total;
        if (target_packet &&
            (packet[0] == LEGACY_CMD_CAPSULE_SN_BROADCAST))
        {
            ++m_antenna.window_valid;
            m_antenna.window_rssi_sum += rssi;
        }
        return true;                                    // 正式扫描只做RSSI统计
    }

    return false;
}

/** @brief 主循环优先处理ISR挂起的0x11和END事件。 */
bool receiver_antenna_service_events(void)
{
	//已经收到TX发送的0x11许可
    if (m_antenna.fast_start_pending)
    {
        /* 先清事件再迁移状态，防止下一轮重复启动扫描。 */
        m_antenna.fast_start_pending = false;
        NRF_LOG_INFO("[SCAN] matched 0x11; starting formal RSSI scan");
        receiver_antenna_start_fast_scan();          //进入正式RSSI扫描
        return true;
    }

	/* end_pending表示在SEEK_END状态轮询天线时，收到了：
		- CRC正确；
		- SN与当前绑定胶囊一致；
		- 命令类型为图片END；*/
    if (m_antenna.end_pending)
    {
        uint8_t image_id = m_antenna.end_frame_id;
        m_antenna.end_pending = false;
        NRF_LOG_INFO("[SCAN] matched END frame=%u on ANT%u",
                     (unsigned)image_id,
                     (unsigned)(rf1662_get_antenna() + 1u));
        if (m_antenna.request_pending && receiver_binding_is_bound())
        {
            if (m_antenna.request_attempts < 2u)
            {
                receiver_antenna_send_fast_scan_request(
                    image_id, receiver_binding_get());
                /* 停在捕获END的天线上，给TX留出接收请求和回复时间。 */
                m_antenna.hold_until_ms = receiver_timebase_now_ms() +
                                          RF1662_SCAN_REQUEST_HOLD_MS;
            }
            else
            {
                m_antenna.request_pending = false;
                NRF_LOG_WARNING("[SCAN] two END request windows used; continue seeking");
            }
        }
        return true;
    }
    return false;
}

/** @brief 按软件节拍推进发现、寻END、正式扫描和锁定监测。 */
/* 未绑定
  │
  ▼
DISCOVERY ──绑定成功──► SEEK_END ──收到0x11──► FAST_SCAN
  ▲                         ▲                       │
  │                         │                       │
解除绑定              无有效候选/候选耗尽       扫描完成
                            │                       │
                            └──── LOCKED ◄──────────┘
                                      │
                          当前天线失联或接收质量差
                                      │
                             下一候选 / SEEK_END */
bool receiver_antenna_service_schedule(void)
{
    uint32_t now;

    if (!m_antenna.service_due)
    {
        return false;
    }
    m_antenna.service_due = false;
    now = receiver_timebase_now_ms();

    /* DISCOVERY：未绑定时定时轮询；一旦绑定立即进入SEEK_END。 */
    if (m_antenna.state == ANTENNA_STATE_DISCOVERY)
    {
        if (receiver_binding_is_bound())
        {
            receiver_antenna_start_seek_end();    //进入协商准备状态，轮询12路寻找绑定胶囊的图片END
        }
		/* 判断当前天线的驻留时间是否结束*/
        else if ((int32_t)(now - m_antenna.deadline_ms) >= 0)
        {
            m_antenna.discovery_holding = false;         //清除“发现SN后延长驻留”的状态。
            m_antenna.antenna = (uint8_t)((m_antenna.antenna + 1u) %
                                           RF1662_ANTENNA_COUNT);        //切换到下一路天线。
            receiver_radio_select_antenna(m_antenna.antenna);
            m_antenna.deadline_ms = now + RF1662_DISCOVERY_DWELL_MS;
        }
        return true;
    }

	// 进入未绑定发现状态，从默认天线逐路寻找任意SN广播
    if (!receiver_binding_is_bound())
    {
        receiver_antenna_start_discovery();              //只要当前处于SEEK_END、FAST_SCAN或LOCKED状态，但绑定关系被解除，就立即返回发现模式
        return true;
    }

    /* SEEK_END：8 ms逐路轮询；等待TX回复期间保持当前天线不动。 */
    if (m_antenna.state == ANTENNA_STATE_SEEK_END)
    {
        if (((int32_t)(now - m_antenna.hold_until_ms) < 0) ||
            ((int32_t)(now - m_antenna.deadline_ms) < 0))
        {
            return false;
        }
        receiver_antenna_advance();		//前进到下一路天线
        if (m_antenna.round >= RF1662_SEEK_END_ROUNDS)
        {
            m_antenna.round = 0u;                       // 未找到END则继续下一批
            if (!m_antenna.request_pending)
            {
                m_antenna.request_pending = true;       // 新一批恢复两次协商机会
                m_antenna.request_attempts = 0u;
            }
        }
		
		//将RF1662切到下一路，并监听8 ms
        receiver_radio_select_antenna(m_antenna.antenna);
        m_antenna.deadline_ms = now + RF1662_SEEK_END_DWELL_MS;
        return true;
    }

    /* FAST_SCAN：每个驻留窗口结束时，归档本路有效样本和RSSI。 */
	/* 当TX回复0x11后，说明TX已经进入快速SN广播阶段。RX开始逐路统计信号质量。*/
    if (m_antenna.state == ANTENNA_STATE_FAST_SCAN)
    {
        if ((int32_t)(now - m_antenna.deadline_ms) < 0)
        {
            return false;
        }

		/* 
		- window_total：当前天线收到的所有无线包数量。
		- window_valid：CRC正确、SN匹配、命令为0x05的有效广播数量。
		- window_rssi_sum：有效广播包RSSI幅值之和。
		保存当前天线的统计结果
		*/
        m_antenna.valid_count[m_antenna.antenna] += m_antenna.window_valid;
        m_antenna.rssi_sum[m_antenna.antenna] += m_antenna.window_rssi_sum;
        receiver_antenna_reset_window();            //清空窗口统计
        receiver_antenna_advance();					//切换到下一路

        if (m_antenna.round < RF1662_FAST_SCAN_ROUNDS)
        {
            receiver_radio_select_antenna(m_antenna.antenna);
            m_antenna.deadline_ms = now + RF1662_FAST_SCAN_DWELL_MS;
        }
        else
        {
			/*
				计算每路天线的平均RSSI。
				优先选择样本数达到2个以上的天线。
				信号强度更好的排在前面。
				最多保留3路候选天线。
			*/
            receiver_antenna_build_candidates();
			
            if (m_antenna.candidate_count != 0u)
            {
				/*
					状态切换为LOCKED。切换到第一候选天线。清空图片重组状态。
					清空旧无线包队列。初始化链路监测时间。清零连续失败帧数。
				*/
                receiver_antenna_lock(0u);
            }
            else
            {
				/*
					说明12路天线在正式扫描阶段都没有收到目标胶囊的有效SN广播。
					此时无法选出天线，因此重新进入SEEK_END，再次寻找END并重新协商。
				*/
                NRF_LOG_WARNING("[SCAN] formal scan has no valid SN samples");
                m_antenna.request_pending = true;
                m_antenna.request_attempts = 0u;
                receiver_antenna_start_seek_end();
            }
        }
        return true;
    }

    /* LOCKED：任一失联条件成立时先试下一候选，候选耗尽再重新协商。 */
	/*  1.	长时间没收到目标胶囊的数据包   1500ms
		2.  长时间没有完整图片             4000ms
		3.  连续失败图片过多               3帧
	*/
    if ((m_antenna.state == ANTENNA_STATE_LOCKED) &&
        (((uint32_t)(now - m_antenna.last_target_packet_ms) >=
          RF1662_TARGET_PACKET_TIMEOUT_MS) ||
         ((uint32_t)(now - m_antenna.last_complete_image_ms) >=
          RF1662_COMPLETE_IMAGE_TIMEOUT_MS) ||
         (m_antenna.failed_frame_count >= RF1662_FAILED_FRAME_LIMIT)))
    {
        NRF_LOG_WARNING("[LINK] weak ANT%u packet_age=%ums image_age=%ums failures=%u",
                        (unsigned)(rf1662_get_antenna() + 1u),
                        (unsigned)(now - m_antenna.last_target_packet_ms),
                        (unsigned)(now - m_antenna.last_complete_image_ms),
                        (unsigned)m_antenna.failed_frame_count);
        receiver_image_reset();                       //清除未完成图片
						
		/*还有下一候选天线*/
        if ((uint8_t)(m_antenna.candidate_index + 1u) <
            m_antenna.candidate_count)
        {
            receiver_antenna_lock((uint8_t)(m_antenna.candidate_index + 1u));
        }
        else
        {
			/* 如果前三个候选全部失败，说明胶囊位置或无线环境可能已经明显变化。*/
            NRF_LOG_WARNING("[LINK] all candidates exhausted; seek END again");
            m_antenna.request_pending = true;
            m_antenna.request_attempts = 0u;
            receiver_antenna_start_seek_end();
        }
        return true;
    }
    return false;
}

/** @brief 绑定状态改变后清理旧事件和RSSI，并切换到对应起始状态。 */
void receiver_antenna_binding_changed(void)
{
    m_antenna.end_pending = false;
    m_antenna.fast_start_pending = false;
    m_antenna.hold_until_ms = 0u;
    memset(m_antenna.latest_rssi, 0, sizeof(m_antenna.latest_rssi));

    if (receiver_binding_is_bound())
    {
        NRF_LOG_INFO("[BIND] active; clear RSSI and seek target END");
        m_antenna.request_pending = true;
        m_antenna.request_attempts = 0u;
        receiver_antenna_start_seek_end();
    }
    else
    {
        NRF_LOG_INFO("[BIND] cleared; return to discovery");
        m_antenna.request_pending = false;
        receiver_antenna_start_discovery();
    }
}

/** @brief 收到0x11后校验绑定SN，并挂起正式扫描启动事件。 */
void receiver_antenna_fast_scan_granted(const uint8_t *sn)
{
    if (receiver_binding_is_bound() && m_antenna.request_pending &&
        receiver_binding_matches(sn))
    {
        NRF_LOG_INFO("[SCAN] accepted matched 0x11 notification");
        m_antenna.request_pending = false;
        m_antenna.request_attempts = 0u;
        m_antenna.fast_start_pending = true;
    }
}

/** @brief 完整图片成功后刷新链路时间，并清零连续失败计数。 */
void receiver_antenna_note_complete_image(void)
{
    if (m_antenna.state == ANTENNA_STATE_LOCKED)
    {
        uint32_t now = receiver_timebase_now_ms();
        m_antenna.last_complete_image_ms = now;
        m_antenna.last_target_packet_ms = now;
        m_antenna.failed_frame_count = 0u;
    }
}

/** @brief 记录一次图片失败，供LOCKED状态判断是否需要换天线。 */
void receiver_antenna_note_frame_failure(void)
{
    if (m_antenna.failed_frame_count < 0xFFu)
    {
        ++m_antenna.failed_frame_count;
    }
}

/** @brief 发现SN后延长当前天线驻留，便于STM和上位机完成发现。 */
void receiver_antenna_note_discovery_sn(void)
{
    if ((m_antenna.state == ANTENNA_STATE_DISCOVERY) &&
        !m_antenna.discovery_holding)
    {
        m_antenna.discovery_holding = true;
        m_antenna.deadline_ms = receiver_timebase_now_ms() +
                                RF1662_DISCOVERY_FOUND_HOLD_MS;
    }
}

/** @brief 在图片END应答窗口内最多发送两次0x12扫描请求。 */
bool receiver_antenna_try_fast_scan_request(uint8_t image_id,
                                            const uint8_t *capsule_sn)
{
    if (!m_antenna.request_pending)
    {
        return false;
    }
    if (m_antenna.request_attempts >= 2u)
    {
        m_antenna.request_pending = false;
        return false;
    }
	//发送0x12，请求TX暂停图片并进入快速SN广播窗口。
    receiver_antenna_send_fast_scan_request(image_id, capsule_sn);
    m_antenna.hold_until_ms = receiver_timebase_now_ms() +
                              RF1662_SCAN_REQUEST_HOLD_MS;
    return true;
}

/** @brief 查询当前是否正在等待一次快速扫描协商。 */
bool receiver_antenna_scan_request_pending(void)
{
    return m_antenna.request_pending;
}

/** @brief 复制最近一次完整扫描结果，供128字节设备信息使用。 */
void receiver_antenna_copy_latest_rssi(
    uint8_t rssi[RF1662_ANTENNA_COUNT])
{
    memcpy(rssi, m_antenna.latest_rssi, RF1662_ANTENNA_COUNT);
}
