/**
 ******************************************************************************
 * @file    app_uart_rx.c
 * @brief   USART2/USART3 IDLE 空闲中断、循环 DMA 与环形缓冲应用实现
 *
 * 整体架构：
 *   ┌─────────┐  IDLE/HT/TC 中断   ┌─────────┐    主循环    ┌──────────┐
 *   │ USART2  │ ──────────────────> │ DMA buf │ ──────────> │ Ring buf │
 *   │ (nRF)   │   把新数据提交到 ring │ 1024B   │   临界区读 │ UART2 8K │
 *   └─────────┘                     └─────────┘              └──────────┘
 *
 * 关键设计点：
 *   1. 双缓冲：DMA 硬件缓冲 + 应用层环形缓冲
 *   2. 临界区：主循环读 ring 时短暂关中断，保证一致性
 *   3. 溢出处理：ring 满时只累加 lost_bytes 计数，不打印（中断内禁止）
 *   4. UART2 缓冲更大：应对 nRF 突发数据（FATFS 阻塞时）
 * @version 1.1.0
 ******************************************************************************
 */

#include "app_uart_rx.h"
#include "app_log.h"
#include "capsule_protocol.h"
#include "ring_buffer.h"
#include "usart.h"

/** @brief 每路 UART 循环 DMA 的底层接收缓冲区大小，单位为字节。
 *  - 必须 ≥ 单次 DMA burst size
 *  - 越大越能容忍长突发，但占用更多 RAM
 */
#define UART_DMA_BUFFER_SIZE       1024U

/** USART2 缓冲大：nRF 板突发数据 + FATFS 阻塞写时不能丢包。 */
#define UART2_RING_BUFFER_SIZE     8192U
/** USART3 缓冲小：PC 命令量少。 */
#define UART3_RING_BUFFER_SIZE     1024U

/** @brief 主循环单次从环形缓冲区取出的最大数据量。 */
#define UART_PROCESS_BLOCK_SIZE     256U

/**
 * @brief 单路 UART DMA 接收通道的运行状态。
 *
 * 完整数据流：
 *   DMA 持续循环写入 dma_buffer
 *   ↓ IDLE / 半满 / 全满 中断
 *   AppUartRx_CommitDmaEvent 把新数据复制到 ring
 *   ↓ 主循环 AppUartRx_Consume
 *   CapsuleProtocol_InputFromNrf / InputFromPc
 */
typedef struct
{
  UART_HandleTypeDef *uart;       /**< 对应的 STM32 HAL UART 句柄。 */
  uint8_t *dma_buffer;            /**< DMA 循环写入的底层存储区。 */
  uint16_t dma_size;              /**< DMA 底层存储区总字节数。 */
  uint16_t dma_last_position;     /**< 上次已提交到 ring 的 DMA 位置。 */
  RingBuffer_t ring;              /**< 供应用层读取的数据环形缓冲区。 */
  const char *name;               /**< 用于日志输出的通道名称。 */
  volatile uint32_t lost_bytes;   /**< ring 空间不足而丢弃的累计字节数。 */
} AppUartRxChannel_t;

/** USART2 DMA 硬件直接写入的循环缓冲区。 */
static uint8_t uart2_dma_buffer[UART_DMA_BUFFER_SIZE];
/** USART3 DMA 硬件直接写入的循环缓冲区。 */
static uint8_t uart3_dma_buffer[UART_DMA_BUFFER_SIZE];
/** USART2 应用层环形缓冲区的实际存储空间。 */
static uint8_t uart2_ring_storage[UART2_RING_BUFFER_SIZE];
/** USART3 应用层环形缓冲区的实际存储空间。 */
static uint8_t uart3_ring_storage[UART3_RING_BUFFER_SIZE];

/** @brief 由TIM1每1ms累加的应用运行时间；当前用于运行时间和帧率统计。 */
static volatile uint32_t application_milliseconds;

/** USART2 接收通道控制块。 */
static AppUartRxChannel_t uart2_channel;
/** USART3 接收通道控制块。 */
static AppUartRxChannel_t uart3_channel;

/**
 * @brief  配置单路 UART DMA 接收通道。
 * @param  channel 通道控制块指针。
 * @param  uart HAL UART 句柄指针。
 * @param  dma_buffer DMA 循环接收存储区。
 * @param  ring_storage 环形缓冲区存储区。
 * @param  ring_size 环形缓冲区字节数。
 * @param  name 日志显示名称。
 * @return 成功返回 1，失败返回 0。
 * @note   不可重入；仅在应用初始化阶段调用。
 *
 * 启动步骤：
 *   ① 保存通道参数到控制块
 *   ② RingBuffer_Init 初始化环形缓冲区
 *   ③ HAL_UARTEx_ReceiveToIdle_DMA 启动 DMA + IDLE 中断接收
 */
static uint8_t AppUartRx_ChannelInit(AppUartRxChannel_t *channel,
                                     UART_HandleTypeDef *uart,
                                     uint8_t *dma_buffer,
                                     uint8_t *ring_storage,
                                     uint16_t ring_size,
                                     const char *name)
{
  /* 保存本通道所使用的外设、内存和日志名称，后续处理函数无需区分 UART2/3。 */
  channel->uart = uart;
  channel->dma_buffer = dma_buffer;
  channel->dma_size = UART_DMA_BUFFER_SIZE;
  channel->dma_last_position = 0U;
  channel->name = name;
  channel->lost_bytes = 0U;

  /* 初始化环形缓冲区（ring_storage 由调用者提供）。 */
  if (RingBuffer_Init(&channel->ring, ring_storage,
                      ring_size) != RING_BUFFER_OK)
  {
    return 0U;
  }

  /*
   * ReceiveToIdle_DMA 同时打开 DMA 接收和 UART IDLE 中断。
   * DMA 保持循环模式，数组写满后硬件自动从头继续接收。
   */
  if (HAL_UARTEx_ReceiveToIdle_DMA(uart, dma_buffer,
                                   UART_DMA_BUFFER_SIZE) != HAL_OK)
  {
    return 0U;
  }
  return 1U;
}

/**
 * @brief  把一次 UART DMA 接收事件对应的新数据提交到环形缓冲区。
 * @param  channel 通道控制块指针。
 * @param  current_position HAL 回调报告的 DMA 当前接收位置。
 * @return None.
 * @note   由 UART/DMA 中断上下文调用，不可重入；不执行任何阻塞操作。
 *
 * 处理两种情况：
 *   - 未回绕：current_position > last_position（线性增长）
 *     → 一次 RingBuffer_Write 复制 [last, current)
 *   - 已回绕：current_position < last_position（循环 DMA 越过末尾）
 *     → 两次 Write：[last, dma_size) + [0, current)
 */
static void AppUartRx_CommitDmaEvent(AppUartRxChannel_t *channel,
                                     uint16_t current_position)
{
  uint16_t received_length;  /* 本次检测到的新增数据总长度。 */
  uint16_t written;          /* 成功写入应用层 ring 的数据长度。 */

  /* 过滤重复事件，并防止异常位置造成数组越界访问。 */
  if ((current_position == channel->dma_last_position) ||
      (current_position > channel->dma_size))
  {
    return;
  }

  if (current_position > channel->dma_last_position)
  {
    /*
     * 未发生 DMA 回绕。新数据在 dma_last_position 与 current_position
     * 之间构成一段连续内存，可一次写入 ring。
     */
    received_length = current_position - channel->dma_last_position;
    written = RingBuffer_Write(&channel->ring,
        &channel->dma_buffer[channel->dma_last_position], received_length);
  }
  else
  {
    /*
     * current_position 小于上次位置，说明循环 DMA 已越过末尾并回到开头。
     * 数据由"上次位置到数组末尾"和"数组开头到当前位置"两段组成。
     */
    received_length = channel->dma_size - channel->dma_last_position;
    written = RingBuffer_Write(&channel->ring,
        &channel->dma_buffer[channel->dma_last_position], received_length);  /* 先复制 DMA 缓冲尾段。 */
    written += RingBuffer_Write(&channel->ring, channel->dma_buffer,
                                current_position);  /* 再复制 DMA 缓冲头段。 */
    received_length += current_position;
  }

  /*
   * 中断里禁止打印日志。ring 空间不足时只累计丢失字节，交给主循环报告。
   */
  if (written < received_length)
  {
    channel->lost_bytes += (uint32_t)(received_length - written);
  }

  /* 无论 ring 是否溢出，都推进同步位置，避免反复处理同一段 DMA 数据。 */
  channel->dma_last_position = current_position;
}

/**
 * @brief  在主循环中消费单路 UART 环形缓冲数据。
 * @param  channel 通道控制块指针。
 * @return None.
 * @note   非线程安全；仅允许在主循环中调用。
 *
 * 临界区保护：
 *   读 ring 时短暂关中断，避免 head/tail/count 在 RingBuffer_Read 中被并发修改。
 *   完成后按进入前的 PRIMASK 状态恢复中断。
 */
static void AppUartRx_Consume(AppUartRxChannel_t *channel)
{
  uint8_t data[UART_PROCESS_BLOCK_SIZE];
  uint16_t length;
  uint32_t lost_bytes;
  uint32_t interrupt_state;

  interrupt_state = __get_PRIMASK();  /* 保存进入临界区前的全局中断状态。 */
  __disable_irq();
  length = RingBuffer_Read(&channel->ring, data, sizeof(data));
  lost_bytes = channel->lost_bytes;
  channel->lost_bytes = 0U;
  if (interrupt_state == 0U)
  {
    __enable_irq();                  /* 仅当进入临界区前中断已开启时才恢复。 */
  }

  if (lost_bytes > 0U)
  {
    LOG_WARNING("%s ring buffer overflow, lost %lu bytes", channel->name,
                (unsigned long)lost_bytes);
  }

  if (length > 0U)
  {
    if (channel == &uart2_channel)
    {
      if (length <= 32U)
      {
        LOG_INFO("[UART2 RX] short block bytes=%u first=%02X %02X %02X %02X",
                 (unsigned int)length,
                 data[0],
                 (length > 1U) ? data[1] : 0U,
                 (length > 2U) ? data[2] : 0U,
                 (length > 3U) ? data[3] : 0U);
      }
      /* USART2 连接 nRF RX；解析器可跨 DMA 分段持续拼接大图像帧。 */
      CapsuleProtocol_InputFromNrf(data, length);
    }
    else
    {
      /* USART3 连接电脑；控制命令透明转发给 nRF RX。 */
      CapsuleProtocol_InputFromPc(data, length);
    }
  }
}

/**
 * @brief  初始化两路 DMA 接收及其环形缓冲区。
 * @param  None.
 * @return 初始化成功返回 1，失败返回 0。
 * @note   不可重入；应在 UART 和 DMA 外设初始化完成后调用一次。
 *
 * 简易流程图：
 *   清零1ms计数并初始化协议
 *       → 初始化USART2的1024B循环DMA和8KB ring
 *       → 初始化USART3的1024B循环DMA和1KB ring
 *       → 两路均成功返回1，否则返回0
 */
uint8_t AppUartRx_Init(void)
{
  uint8_t result = 1U;

  /* 应用层时间从串口接收模块初始化前开始清零。 */
  application_milliseconds = 0U;
  CapsuleProtocol_Init();

  /* UART2 与 UART3 共用相同通道逻辑，分别持有自己的 DMA 和 ring 存储区。 */
  result &= AppUartRx_ChannelInit(&uart2_channel, &huart2,
                                  uart2_dma_buffer, uart2_ring_storage,
                                  UART2_RING_BUFFER_SIZE,
                                  "UART2");
  result &= AppUartRx_ChannelInit(&uart3_channel, &huart3,
                                  uart3_dma_buffer, uart3_ring_storage,
                                  UART3_RING_BUFFER_SIZE,
                                  "UART3");
  return result;
}

/**
 * @brief  在主循环中消费由 DMA 接收事件提交到环形缓冲区的数据。
 * @param  None.
 * @return None.
 * @note   非线程安全；仅允许在主循环中调用。
 *
 * 简易流程图：
 *   消费USART2 ring → 输入nRF图片/控制解析器
 *       → 消费USART3 ring → 输入PC命令解析器
 *       → 处理USART3 DMA完成/错误及等待转发的控制应答
 */
void AppUartRx_Process(void)
{
  /* DMA 新数据已在 IDLE/半满/满中断回调中提交，此处仅做应用层消费。 */
  AppUartRx_Consume(&uart2_channel);
  AppUartRx_Consume(&uart3_channel);
  CapsuleProtocol_Process();
}

/**
 * @brief  HAL UART DMA 接收事件回调。
 * @param  huart 产生 IDLE、DMA 半满或 DMA 满事件的 UART 句柄。
 * @param  size DMA 缓冲区中当前接收位置。
 * @return None.
 * @note   由中断上下文调用，不可重入；函数内不执行日志或其他阻塞操作。
 *
 * 触发时机：
 *   - UART 空闲线路（IDLE 中断）
 *   - DMA 半满（HT 中断）
 *   - DMA 全满（TC 中断）
 */
void HAL_UARTEx_RxEventCallback(UART_HandleTypeDef *huart, uint16_t size)
{
  if (huart->Instance == USART2)
  {
    AppUartRx_CommitDmaEvent(&uart2_channel, size);
  }
  else if (huart->Instance == USART3)
  {
    AppUartRx_CommitDmaEvent(&uart3_channel, size);
  }
  else
  {
    /* USART1 仅用于日志输出，不参与 DMA 接收事件处理。 */
  }
}

/**
 * @brief  处理 TIM1 的 1 ms 周期事件。
 * @param  None.
 * @return None.
 * @note   由中断上下文调用，不可重入；函数内不得执行阻塞操作。
 */
void AppUartRx_Timer1msCallback(void)
{
  /* 仅做固定时间的计数操作，保证 TIM1 中断服务时间尽可能短。 */
  application_milliseconds++;
}

/**
 * @brief  获取应用运行毫秒计数。
 * @param  None.
 * @return 自应用定时器启动后的毫秒计数值。
 * @note   可重入；32 位 Cortex-M3 对齐读写为原子操作。
 */
uint32_t AppUartRx_GetMilliseconds(void)
{
  /* Cortex-M3 对齐的 32 位读取不会被拆分，可直接返回中断更新的计数值。 */
  return application_milliseconds;
}
