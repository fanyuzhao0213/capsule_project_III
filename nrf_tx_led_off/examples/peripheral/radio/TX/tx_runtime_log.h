#ifndef TX_RUNTIME_LOG_H
#define TX_RUNTIME_LOG_H

#include <stdint.h>

void tx_runtime_log_sn_broadcast(const uint8_t sn[8]);
void tx_runtime_log_image_broadcast(uint16_t frame_id, uint16_t image_size,
                                    uint16_t fragment_count);
void tx_runtime_log_image_rejected(uint16_t image_size, uint16_t maximum_size);
void tx_runtime_log_ack_received(uint16_t frame_id);
void tx_runtime_log_ack_timeout(uint16_t frame_id, uint8_t retry_count,
                                uint8_t maximum_retries);
#endif
