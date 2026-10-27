/**
 ******************************************************************************
 * @file    ring_buffer.h
 * @copyright Copyright (c) 2026
 * @author  Capsule Project Team
 * @brief   通用字节环形缓冲区接口。
 * @version 1.0.0
 ******************************************************************************
 */

#ifndef RING_BUFFER_H
#define RING_BUFFER_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

/** @brief 环形缓冲区操作结果。 */
typedef enum
{
  RING_BUFFER_OK = 0,
  RING_BUFFER_ERROR,
  RING_BUFFER_FULL,
  RING_BUFFER_EMPTY
} RingBufferStatus_t;

/** @brief 通用字节环形缓冲区控制块。 */
typedef struct
{
  uint8_t *buffer;
  uint16_t size;
  volatile uint16_t head;
  volatile uint16_t tail;
  volatile uint16_t count;
} RingBuffer_t;

/**
 * @brief  初始化环形缓冲区。
 * @param  ring 环形缓冲区控制块指针。
 * @param  storage 用户提供的存储区指针。
 * @param  size 存储区字节数。
 * @return 初始化结果，参数有效时返回 RING_BUFFER_OK。
 * @note   不可重入；应在使用该缓冲区前调用一次。
 */
RingBufferStatus_t RingBuffer_Init(RingBuffer_t *ring, uint8_t *storage,
                                   uint16_t size);

/**
 * @brief  向环形缓冲区写入数据。
 * @param  ring 环形缓冲区控制块指针。
 * @param  data 待写入数据指针。
 * @param  length 待写入字节数。
 * @return 实际写入字节数。
 * @note   非线程安全；中断与主循环共享时，调用者必须提供临界区保护。
 */
uint16_t RingBuffer_Write(RingBuffer_t *ring, const uint8_t *data,
                          uint16_t length);

/**
 * @brief  从环形缓冲区读取数据。
 * @param  ring 环形缓冲区控制块指针。
 * @param  data 接收数据的目标存储区指针。
 * @param  length 希望读取的最大字节数。
 * @return 实际读取字节数。
 * @note   非线程安全；中断与主循环共享时，调用者必须提供临界区保护。
 */
uint16_t RingBuffer_Read(RingBuffer_t *ring, uint8_t *data, uint16_t length);

/**
 * @brief  获取环形缓冲区内的有效数据量。
 * @param  ring 环形缓冲区控制块指针。
 * @return 有效数据字节数；参数无效时返回 0。
 * @note   读取操作可重入，但跨上下文使用时返回值仅代表读取瞬间的状态。
 */
uint16_t RingBuffer_Available(const RingBuffer_t *ring);

/**
 * @brief  清空环形缓冲区。
 * @param  ring 环形缓冲区控制块指针。
 * @return None.
 * @note   非线程安全；清空时不得有其他上下文访问同一缓冲区。
 */
void RingBuffer_Clear(RingBuffer_t *ring);

#ifdef __cplusplus
}
#endif

#endif /* RING_BUFFER_H */
