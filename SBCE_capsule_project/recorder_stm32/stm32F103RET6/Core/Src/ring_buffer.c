/**
 ******************************************************************************
 * @file    ring_buffer.c
 * @copyright Copyright (c) 2026
 * @author  Capsule Project Team
 * @brief   通用字节环形缓冲区实现。
 * @version 1.0.0
 ******************************************************************************
 */

#include "ring_buffer.h"

/**
 * @brief  初始化环形缓冲区。
 * @param  ring 环形缓冲区控制块指针。
 * @param  storage 用户提供的存储区指针。
 * @param  size 存储区字节数。
 * @return 初始化结果，参数有效时返回 RING_BUFFER_OK。
 * @note   不可重入；应在使用该缓冲区前调用一次。
 */
RingBufferStatus_t RingBuffer_Init(RingBuffer_t *ring, uint8_t *storage,
                                   uint16_t size)
{
  if ((ring == 0) || (storage == 0) || (size == 0U))
  {
    return RING_BUFFER_ERROR;
  }

  ring->buffer = storage;
  ring->size = size;
  ring->head = 0U;
  ring->tail = 0U;
  ring->count = 0U;
  return RING_BUFFER_OK;
}

/**
 * @brief  向环形缓冲区写入数据。
 * @param  ring 环形缓冲区控制块指针。
 * @param  data 待写入数据指针。
 * @param  length 待写入字节数。
 * @return 实际写入字节数。
 * @note   非线程安全；中断与主循环共享时，调用者必须提供临界区保护。
 *
 * 简易流程图：
 *   参数有效且缓冲未满
 *       → 写buffer[head] → head加1并按size回绕 → count加1
 *       → 数据写完或缓冲满时返回实际写入数
 */
uint16_t RingBuffer_Write(RingBuffer_t *ring, const uint8_t *data,
                          uint16_t length)
{
  uint16_t written = 0U;

  if ((ring == 0) || (data == 0) || (ring->buffer == 0) || (ring->size == 0U))
  {
    return 0U;
  }

  while ((written < length) && (ring->count < ring->size))
  {
    ring->buffer[ring->head] = data[written]; /* head 指向下一处可写字节。 */
    ring->head++;
    if (ring->head >= ring->size)
    {
      ring->head = 0U; /* 到达存储区末尾后回绕到首地址。 */
    }
    ring->count++;
    written++;
  }

  return written;
}

/**
 * @brief  从环形缓冲区读取数据。
 * @param  ring 环形缓冲区控制块指针。
 * @param  data 接收数据的目标存储区指针。
 * @param  length 希望读取的最大字节数。
 * @return 实际读取字节数。
 * @note   非线程安全；中断与主循环共享时，调用者必须提供临界区保护。
 *
 * 简易流程图：
 *   参数有效且count>0
 *       → 读buffer[tail] → tail加1并按size回绕 → count减1
 *       → 达到请求长度或缓冲为空时返回实际读取数
 */
uint16_t RingBuffer_Read(RingBuffer_t *ring, uint8_t *data, uint16_t length)
{
  uint16_t read_length = 0U;

  if ((ring == 0) || (data == 0) || (ring->buffer == 0) || (ring->size == 0U))
  {
    return 0U;
  }

  while ((read_length < length) && (ring->count > 0U))
  {
    data[read_length] = ring->buffer[ring->tail]; /* tail 指向下一处待读字节。 */
    ring->tail++;
    if (ring->tail >= ring->size)
    {
      ring->tail = 0U; /* 到达存储区末尾后回绕到首地址。 */
    }
    ring->count--;
    read_length++;
  }

  return read_length;
}

/**
 * @brief  获取环形缓冲区内的有效数据量。
 * @param  ring 环形缓冲区控制块指针。
 * @return 有效数据字节数；参数无效时返回 0。
 * @note   读取操作可重入，但跨上下文使用时返回值仅代表读取瞬间的状态。
 */
uint16_t RingBuffer_Available(const RingBuffer_t *ring)
{
  if (ring == 0)
  {
    return 0U;
  }
  return ring->count;
}

/**
 * @brief  清空环形缓冲区。
 * @param  ring 环形缓冲区控制块指针。
 * @return None.
 * @note   非线程安全；清空时不得有其他上下文访问同一缓冲区。
 */
void RingBuffer_Clear(RingBuffer_t *ring)
{
  if (ring != 0)
  {
    ring->head = 0U;
    ring->tail = 0U;
    ring->count = 0U;
  }
}
