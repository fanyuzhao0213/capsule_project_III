/**
 * @file image.c
 * @brief 图片业务模块；不直接管理Radio、天线或UART硬件。
 */

#include "image.h"
#include "antenna_manager.h"
#include "binding_storage.h"
#include "config.h"
#include "legacy_protocol.h"
#include "nrf.h"
#include "nrf_log.h"
#include "radio_link.h"
#include "receiver_timebase.h"
#include "rf1662.h"
#include "uart_bridge.h"
#include <stdbool.h>
#include <string.h>

#define IMAGE_FRAGMENT_COUNT_MAX \
    ((LEGACY_IMAGE_MAX_SIZE + LEGACY_IMAGE_PACKET_PAYLOAD_SIZE - 1u) / \
     LEGACY_IMAGE_PACKET_PAYLOAD_SIZE)

typedef struct
{
    struct
    {
        bool active;
        uint8_t id;
        uint8_t capsule_sn[LEGACY_CAPSULE_SN_SIZE];
        uint16_t length;
        uint16_t fragment_count;
        uint16_t received_count;
        uint8_t version_main;
        uint8_t version_sub;
        uint8_t version_test;
        bool accel_valid;
        int16_t accel_x;
        int16_t accel_y;
        int16_t accel_z;
        uint8_t received_map[IMAGE_FRAGMENT_COUNT_MAX];
        uint8_t jpeg[LEGACY_IMAGE_MAX_SIZE];
    } frame;
    uint8_t last_capsule_sn[LEGACY_CAPSULE_SN_SIZE];
    bool last_capsule_sn_valid;

    uint32_t radio_begin_ms;
    uint32_t radio_duration_ms;
    uint32_t uart_begin_ms;
    uint32_t last_forward_ms;
    bool have_last_forward;

    uint32_t complete_count;
    uint32_t forwarded_count;
    uint32_t uart_busy_drop_count;
    uint32_t incomplete_count;
    uint32_t checksum_failure_count;
    uint32_t replaced_count;
} receiver_image_state_t;

static receiver_image_state_t m_image;

/** @brief 计算毫秒差值，利用无符号减法兼容32位时间回绕。 */
static uint32_t receiver_elapsed_ms(uint32_t start, uint32_t end)
{
    return (uint32_t)(end - start);
}

/** @brief 合成随图片发给STM的128字节设备信息。 */
static void receiver_image_build_device_info(uint8_t *info)
{
    static const uint8_t marker[LEGACY_DEVICE_INFO_MARKER_SIZE] =
        {0x00u, 0x55u, 0xAAu, 0x88u, 0x99u};
    const uint8_t *sn = m_image.frame.capsule_sn;
    const uint8_t *device_id = (const uint8_t *)NRF_FICR->DEVICEID;
    uint8_t rssi[RF1662_ANTENNA_COUNT];
    uint8_t i;

    /* 先清零未使用和暂未实现字段，防止上一帧数据残留。 */
    memset(info, 0, LEGACY_DEVICE_INFO_SIZE);
    memcpy(&info[LEGACY_DEVICE_INFO_MARKER_OFFSET], marker, sizeof(marker));
    info[LEGACY_DEVICE_INFO_TX_VERSION_MAIN_OFFSET] = m_image.frame.version_main;
    info[LEGACY_DEVICE_INFO_TX_VERSION_SUB_OFFSET] = m_image.frame.version_sub;
    info[LEGACY_DEVICE_INFO_TX_VERSION_TEST_OFFSET] = m_image.frame.version_test;
    info[LEGACY_DEVICE_INFO_RX_VERSION_MAIN_OFFSET] = VERSION_MAIN;
    info[LEGACY_DEVICE_INFO_RX_VERSION_SUB_OFFSET] = VERSION_SUB;
    info[LEGACY_DEVICE_INFO_RX_VERSION_TEST_OFFSET] = VERSION_TEST;

    if (m_image.last_capsule_sn_valid &&
        (memcmp(m_image.last_capsule_sn, sn, LEGACY_CAPSULE_SN_SIZE) == 0))
    {
        sn = m_image.last_capsule_sn;
    }
    memcpy(&info[LEGACY_DEVICE_INFO_CAPSULE_SN_OFFSET], sn,
           LEGACY_CAPSULE_SN_SIZE);
    memcpy(&info[LEGACY_DEVICE_INFO_RX_DEVICE_ID_OFFSET], device_id,
           LEGACY_DEVICE_INFO_RX_DEVICE_ID_SIZE);

    /* RSSI保存最近正式扫描值；锁定天线的值会由实时目标包继续刷新。 */
    receiver_antenna_copy_latest_rssi(rssi);
    for (i = 0u; i < 6u; ++i)
    {
        info[LEGACY_DEVICE_INFO_ANT_RSSI_1_6_OFFSET + i] = rssi[i];
        info[LEGACY_DEVICE_INFO_ANT_RSSI_7_12_OFFSET + i] = rssi[i + 6u];
    }
    info[LEGACY_DEVICE_INFO_ACTIVE_ANTENNA_OFFSET] =
        (uint8_t)(rf1662_get_antenna() + 1u);
    info[LEGACY_DEVICE_INFO_RADIO_FREQUENCY_OFFSET] =
        (uint8_t)RADIO_FREQUENCY_OFFSET;
    info[LEGACY_DEVICE_INFO_ACCEL_VALID_OFFSET] =
        m_image.frame.accel_valid ? 1u : 0u;
    LegacyProtocol_PutI16Be(&info[LEGACY_DEVICE_INFO_ACCEL_X_OFFSET],
                            m_image.frame.accel_x);
    LegacyProtocol_PutI16Be(&info[LEGACY_DEVICE_INFO_ACCEL_Y_OFFSET],
                            m_image.frame.accel_y);
    LegacyProtocol_PutI16Be(&info[LEGACY_DEVICE_INFO_ACCEL_Z_OFFSET],
                            m_image.frame.accel_z);

#if RX_DEVICE_INFO_LOG_ENABLED
    NRF_LOG_INFO("[DEVICE_INFO RX] frame=%u bytes=128",
                 (unsigned)m_image.frame.id);
    NRF_LOG_HEXDUMP_INFO(info, LEGACY_DEVICE_INFO_SIZE);
#endif
}

/** @brief 图片完成后优先发0x12扫描请求，否则发送普通0x10 ACK。 */
static void receiver_image_send_ack(void)
{
    uint8_t packet[RADIO_PACKET_SIZE] __ALIGNED(4) = {0};

    if (receiver_antenna_try_fast_scan_request(
            m_image.frame.id, m_image.frame.capsule_sn))
    {
        return;
    }

    packet[0] = LEGACY_CMD_IMAGE_RECEIVED_RESPONSE;
    packet[1] = m_image.frame.id;
    memcpy(&packet[2], m_image.frame.capsule_sn, LEGACY_CAPSULE_SN_SIZE);
    (void)receiver_radio_send(packet, RADIO_PACKET_SIZE, 2u);
}

/** @brief 校验BEGIN参数并创建新的图片重组上下文。 */
static bool receiver_image_begin(const uint8_t *packet)
{
    uint16_t length = LegacyProtocol_GetU16Be(&packet[10]);
    uint16_t fragments = LegacyProtocol_GetU16Be(&packet[12]);
    uint16_t expected = (uint16_t)((length +
        LEGACY_IMAGE_PACKET_PAYLOAD_SIZE - 1u) /
        LEGACY_IMAGE_PACKET_PAYLOAD_SIZE);

    /* 声明的分片数必须与JPEG长度计算结果完全一致。 */
    if ((length == 0u) || (length > LEGACY_IMAGE_MAX_SIZE) ||
        (fragments == 0u) || (fragments > IMAGE_FRAGMENT_COUNT_MAX) ||
        (fragments != expected))
    {
        NRF_LOG_WARNING("Image BEGIN rejected: length=%u fragments=%u",
                        (unsigned)length, (unsigned)fragments);
        return false;
    }

    /* 新帧覆盖未完成旧帧时计为一次链路失败。 */
    if (m_image.frame.active && (m_image.frame.id != packet[1]))
    {
        ++m_image.replaced_count;
        receiver_antenna_note_frame_failure();
    }

    /* BEGIN可能按协议重复发送，重新初始化同一帧不会累计旧分片。 */
    memset(&m_image.frame, 0, sizeof(m_image.frame));
    m_image.frame.active = true;
    m_image.frame.id = packet[1];
    memcpy(m_image.frame.capsule_sn, &packet[2], LEGACY_CAPSULE_SN_SIZE);
    m_image.frame.length = length;
    m_image.frame.fragment_count = fragments;
    m_image.frame.version_main = packet[14];
    m_image.frame.version_sub = packet[15];
    m_image.frame.version_test = packet[16];
    m_image.frame.accel_valid =
        (packet[LEGACY_BEGIN_ACCEL_VALID_OFFSET] == 1u);
    m_image.frame.accel_x =
        LegacyProtocol_GetI16Be(&packet[LEGACY_BEGIN_ACCEL_X_OFFSET]);
    m_image.frame.accel_y =
        LegacyProtocol_GetI16Be(&packet[LEGACY_BEGIN_ACCEL_Y_OFFSET]);
    m_image.frame.accel_z =
        LegacyProtocol_GetI16Be(&packet[LEGACY_BEGIN_ACCEL_Z_OFFSET]);
    m_image.radio_begin_ms = receiver_timebase_now_ms();
    NRF_LOG_INFO("[IMAGE] BEGIN id=%u bytes=%u fragments=%u",
                 (unsigned)m_image.frame.id,
                 (unsigned)m_image.frame.length,
                 (unsigned)m_image.frame.fragment_count);
    NRF_LOG_INFO("[IMAGE] TX version=%u.%u.%u",
                 (unsigned)m_image.frame.version_main,
                 (unsigned)m_image.frame.version_sub,
                 (unsigned)m_image.frame.version_test);
    NRF_LOG_INFO("[IMAGE] accel valid=%u x=%d y=%d z=%d",
                 m_image.frame.accel_valid ? 1u : 0u,
                 (int)m_image.frame.accel_x,
                 (int)m_image.frame.accel_y,
                 (int)m_image.frame.accel_z);
    return true;
}

/** @brief 按分片序号写入JPEG缓冲，重复分片直接忽略。 */
static void receiver_image_store_fragment(const uint8_t *packet)
{
    uint16_t index = LegacyProtocol_GetU16Be(&packet[10]);
    uint16_t offset;
    uint16_t length;

    if ((index >= m_image.frame.fragment_count) ||
        (m_image.frame.received_map[index] != 0u))
    {
        return;
    }

    /* 最后一片只复制JPEG剩余长度，避免把Radio填充字节写入图片。 */
    offset = (uint16_t)(index * LEGACY_IMAGE_PACKET_PAYLOAD_SIZE);
    length = (uint16_t)(m_image.frame.length - offset);
    if (length > LEGACY_IMAGE_PACKET_PAYLOAD_SIZE)
    {
        length = LEGACY_IMAGE_PACKET_PAYLOAD_SIZE;
    }
    memcpy(&m_image.frame.jpeg[offset],
           &packet[LEGACY_IMAGE_PACKET_HEADER_SIZE], length);
    m_image.frame.received_map[index] = 1u;
    ++m_image.frame.received_count;
}

/** @brief 处理END：检查完整性和校验和，ACK后排队发送STM。 */
static void receiver_image_finish(const uint8_t *packet)
{
    uint8_t checksum = 0u;
    uint8_t device_info[LEGACY_DEVICE_INFO_SIZE];
    uint16_t i;

    /* 分片不全时不交付图片；扫描协商期间仍可利用END发送0x12。 */
    if (m_image.frame.received_count != m_image.frame.fragment_count)
    {
        ++m_image.incomplete_count;
        NRF_LOG_WARNING("Image incomplete: id=%u received=%u/%u",
                        (unsigned)m_image.frame.id,
                        (unsigned)m_image.frame.received_count,
                        (unsigned)m_image.frame.fragment_count);
        if (receiver_antenna_scan_request_pending())
        {
            receiver_image_send_ack();
        }
        return;
    }

    /* END[10]是整幅JPEG的8位累加校验值。 */
    for (i = 0u; i < m_image.frame.length; ++i)
    {
        checksum = (uint8_t)(checksum + m_image.frame.jpeg[i]);
    }
    if (checksum != packet[10])
    {
        ++m_image.checksum_failure_count;
        NRF_LOG_WARNING("[IMAGE] checksum failed id=%u calc=%02x rx=%02x",
                        (unsigned)m_image.frame.id,
                        (unsigned)checksum, (unsigned)packet[10]);
        m_image.frame.active = false;
        receiver_antenna_note_frame_failure();
        return;
    }

    ++m_image.complete_count;
    m_image.radio_duration_ms = receiver_elapsed_ms(
        m_image.radio_begin_ms, receiver_timebase_now_ms());
    NRF_LOG_INFO("[IMAGE] complete id=%u bytes=%u radio=%ums",
                 (unsigned)m_image.frame.id,
                 (unsigned)m_image.frame.length,
                 (unsigned)m_image.radio_duration_ms);
    /* 先在TX应答窗口内回包，再进行设备信息组装和UART排队。 */
    receiver_image_send_ack();
    receiver_antenna_note_complete_image();
    receiver_image_build_device_info(device_info);
    if (receiver_uart_queue_image(device_info, m_image.frame.jpeg,
                                  m_image.frame.length, m_image.frame.id))
    {
        m_image.uart_begin_ms = receiver_timebase_now_ms();
        NRF_LOG_INFO("[UART] image queued id=%u jpeg=%u",
                     (unsigned)m_image.frame.id,
                     (unsigned)m_image.frame.length);
    }
    else
    {
        ++m_image.uart_busy_drop_count;
        NRF_LOG_WARNING("[UART] image dropped id=%u: previous frame busy",
                        (unsigned)m_image.frame.id);
    }
    m_image.frame.active = false;
}

/** @brief 初始化图片重组状态和运行统计。 */
void receiver_image_init(void)
{
    memset(&m_image, 0, sizeof(m_image));
}

/** @brief 放弃当前重组帧，保留历史SN和性能统计。 */
void receiver_image_reset(void)
{
    memset(&m_image.frame, 0, sizeof(m_image.frame));
}

/** @brief 缓存最近收到的胶囊SN，供设备信息字段复核使用。 */
void receiver_image_update_capsule_sn(const uint8_t *capsule_sn)
{
    if (capsule_sn != NULL)
    {
        memcpy(m_image.last_capsule_sn, capsule_sn, LEGACY_CAPSULE_SN_SIZE);
        m_image.last_capsule_sn_valid = true;
    }
}

/** @brief 分派绑定胶囊的BEGIN、DATA和END图片包。 */
void receiver_image_process_packet(const uint8_t *packet)
{
    bool image_packet;

    if (packet == NULL)
    {
        return;
    }
    image_packet = (packet[0] == LEGACY_CMD_IMAGE_BEGIN) ||
                   (packet[0] == LEGACY_CMD_IMAGE_DATA) ||
                   (packet[0] == LEGACY_CMD_IMAGE_END);
    /* 图片包SN固定从下标2开始，非绑定胶囊的数据全部丢弃。 */
    if (!image_packet || !receiver_binding_matches(&packet[2]))
    {
        return;
    }

    if (packet[0] == LEGACY_CMD_IMAGE_BEGIN)
    {
        (void)receiver_image_begin(packet);
        return;
    }

    /* SEEK_END捕获的孤立END无需已有BEGIN，也可用于发起快速扫描。 */
    if ((packet[0] == LEGACY_CMD_IMAGE_END) &&
        receiver_antenna_scan_request_pending() &&
        (!m_image.frame.active || (packet[1] != m_image.frame.id)))
    {
        (void)receiver_antenna_try_fast_scan_request(packet[1], &packet[2]);
        return;
    }

    if (!m_image.frame.active || (packet[1] != m_image.frame.id) ||
        (memcmp(&packet[2], m_image.frame.capsule_sn,
                LEGACY_CAPSULE_SN_SIZE) != 0))
    {
        return;
    }

    if (packet[0] == LEGACY_CMD_IMAGE_DATA)
    {
        receiver_image_store_fragment(packet);
    }
    else if (packet[0] == LEGACY_CMD_IMAGE_END)
    {
        receiver_image_finish(packet);
    }
}

/** @brief UART整帧发送完成后更新帧率、耗时和转发计数。 */
void receiver_image_note_stm_forwarded(uint8_t image_id,
                                       uint16_t image_length)
{
    uint32_t now = receiver_timebase_now_ms();
    ++m_image.forwarded_count;

#if RX_FRAME_RATE_LOG_ENABLED
    if (m_image.have_last_forward)
    {
        uint32_t interval = receiver_elapsed_ms(m_image.last_forward_ms, now);
        uint32_t fps_x100 = (interval != 0u) ? (100000u / interval) : 0u;
        NRF_LOG_INFO("[FPS] frame=%u interval=%ums rate=%u.%02u fps",
                     (unsigned)image_id, (unsigned)interval,
                     (unsigned)(fps_x100 / 100u),
                     (unsigned)(fps_x100 % 100u));
    }
    NRF_LOG_INFO("[PERF] frame=%u bytes=%u radio=%ums uart=%ums",
                 (unsigned)image_id, (unsigned)image_length,
                 (unsigned)m_image.radio_duration_ms,
                 (unsigned)receiver_elapsed_ms(m_image.uart_begin_ms, now));
    NRF_LOG_INFO("[PERF] complete=%u forwarded=%u drop=%u",
                 (unsigned)m_image.complete_count,
                 (unsigned)m_image.forwarded_count,
                 (unsigned)m_image.uart_busy_drop_count);
#else
    (void)image_id;
    (void)image_length;
#endif
    m_image.last_forward_ms = now;
    m_image.have_last_forward = true;
    NRF_LOG_INFO("[UART] image sent id=%u jpeg=%u",
                 (unsigned)image_id, (unsigned)image_length);
}
