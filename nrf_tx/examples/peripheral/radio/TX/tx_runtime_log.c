#include "tx_runtime_log.h"
#include "config.h"
#include "nrf_log.h"

void tx_runtime_log_sn_broadcast(const uint8_t sn[8])
{
#if TX_LOG_ENABLED && TX_RUNTIME_LOG_ENABLED
    NRF_LOG_INFO("[SN] broadcast (8 bytes):");
    NRF_LOG_HEXDUMP_INFO(sn, 8u);
#else
    (void)sn;
#endif
}

void tx_runtime_log_image_broadcast(uint16_t frame_id, uint16_t image_size,
                                    uint16_t fragment_count)
{
#if TX_LOG_ENABLED && TX_RUNTIME_LOG_ENABLED
    NRF_LOG_INFO("[IMAGE] broadcast frame=%u bytes=%u fragments=%u",
                 (unsigned)frame_id, (unsigned)image_size,
                 (unsigned)fragment_count);
#else
    (void)frame_id;
    (void)image_size;
    (void)fragment_count;
#endif
}

void tx_runtime_log_image_rejected(uint16_t image_size, uint16_t maximum_size)
{
#if TX_LOG_ENABLED && TX_RUNTIME_LOG_ENABLED
    NRF_LOG_WARNING("[IMAGE] rejected before RF: bytes=%u max=%u",
                    (unsigned)image_size, (unsigned)maximum_size);
#else
    (void)image_size;
    (void)maximum_size;
#endif
}

void tx_runtime_log_ack_received(uint16_t frame_id)
{
#if TX_LOG_ENABLED && TX_RUNTIME_LOG_ENABLED
    NRF_LOG_INFO("[ACK] received frame=%u", (unsigned)frame_id);
#else
    (void)frame_id;
#endif
}

void tx_runtime_log_ack_timeout(uint16_t frame_id, uint8_t retry_count,
                                uint8_t maximum_retries)
{
#if TX_LOG_ENABLED && TX_RUNTIME_LOG_ENABLED
    NRF_LOG_WARNING("[ACK] timeout frame=%u retry=%u/%u",
                    (unsigned)frame_id, (unsigned)retry_count,
                    (unsigned)maximum_retries);
#else
    (void)frame_id;
    (void)retry_count;
    (void)maximum_retries;
#endif
}
