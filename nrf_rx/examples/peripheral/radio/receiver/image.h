/**
 * @file image.h
 * @brief 绑定图片的重组、校验、应答和设备信息生成。
 */

#ifndef RX_IMAGE_H
#define RX_IMAGE_H

#include <stdint.h>

/** @brief 初始化图片接收模块。 */
void receiver_image_init(void);

/** @brief 放弃当前未完成图片，不影响历史统计。 */
void receiver_image_reset(void);

/** @brief 处理一个CRC正确且未被控制模块消费的Radio包。 */
void receiver_image_process_packet(const uint8_t *packet);

/** @brief 缓存SN广播中的8字节SN，供设备信息使用。 */
void receiver_image_update_capsule_sn(const uint8_t *capsule_sn);

/** @brief UART确认整幅图片已经实际发送完成。 */
void receiver_image_note_stm_forwarded(uint8_t image_id,
                                       uint16_t image_length);

#endif /* RX_IMAGE_H */
