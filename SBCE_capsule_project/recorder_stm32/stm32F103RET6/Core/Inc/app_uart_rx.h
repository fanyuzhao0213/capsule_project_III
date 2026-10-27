/**
 ******************************************************************************
 * @file    app_uart_rx.h
 * @brief   USART2/USART3 DMA 接收应用层接口
 *
 * 数据路径：
 *   UART 外设 → 循环 DMA → IDLE/DMA 半满/全满中断 → 环形缓冲区 → 主循环协议解析
 *
 * 关键设计：
 *   - 双缓冲：DMA 硬件缓冲（1024B）+ 应用层环形缓冲（UART2=8KB, UART3=1KB）
 *   - UART2 缓冲大：nRF 接收板可能突发大量数据（FATFS 写 SD 卡阻塞时）
 *   - UART3 缓冲小：PC 端命令少
 *   - 临界区保护：主循环读 ring 时短暂关中断
 * @version 1.0.0
 ******************************************************************************
 */

#ifndef APP_UART_RX_H
#define APP_UART_RX_H

#ifdef __cplusplus
extern "C" {
#endif

#include "stm32f1xx_hal.h"
#include <stdint.h>

/**
 * @defgroup APP_UART_RX UART DMA 接收应用层
 * @brief USART2/USART3 IDLE 空闲中断、循环 DMA、环形缓冲及主循环处理接口。
 *
 * 数据路径：UART 外设 → 循环 DMA → IDLE/半满/满事件 → Ring Buffer
 *           → 主循环协议处理。
 * @{
 */

/**
 * @brief  初始化两路 DMA 接收及其环形缓冲区。
 * @param  None.
 * @return 初始化成功返回 1，失败返回 0。
 * @note   不可重入；应在 UART 和 DMA 外设初始化完成后调用一次。
 *
 * 初始化内容：
 *   - 初始化 UART2 通道（1Mbps 8N1，DMA 循环接收 1024B，ring 8KB）
 *   - 初始化 UART3 通道（921600 bit/s 8N1，DMA 循环接收 1024B，ring 1KB）
 *   - 启动 HAL_UARTEx_ReceiveToIdle_DMA 接收
 *   - 调用 CapsuleProtocol_Init() 初始化协议解析器
 *   - 清零 application_milliseconds 计数器
 */
uint8_t AppUartRx_Init(void);

/**
 * @brief  在主循环中消费由 DMA 接收事件提交到环形缓冲区的数据。
 * @param  None.
 * @return None.
 * @note   非线程安全；仅允许在主循环中调用。
 *
 * 处理流程：
 *   - 消费 UART2 ring → CapsuleProtocol_InputFromNrf（nRF 板 → STM 解析）
 *   - 消费 UART3 ring → CapsuleProtocol_InputFromPc（PC → STM 中转给 nRF 板）
 *   - 调用 CapsuleProtocol_Process() 报告 DMA 完成/错误事件
 */
void AppUartRx_Process(void);

/**
 * @brief  HAL UART DMA 接收事件回调。
 * @param  huart 产生 IDLE、DMA 半满或 DMA 满事件的 UART 句柄。
 * @param  size DMA 缓冲区中当前接收位置（HAL 报告）。
 * @return None.
 * @note   由中断上下文调用，不可重入；函数内不执行日志或其他阻塞操作。
 *
 * 触发时机：
 *   - UART 空闲线路（IDLE 中断）→ 当前 DMA 位置 = size
 *   - DMA 半满（HT 中断）
 *   - DMA 全满（TC 中断）
 *
 * 处理：把 dma_last_position 到 size 的新数据复制到 ring
 */
void HAL_UARTEx_RxEventCallback(UART_HandleTypeDef *huart, uint16_t size);

/**
 * @brief  处理 TIM1 的 1 ms 周期事件。
 * @param  None.
 * @return None.
 * @note   由中断上下文调用，不可重入；函数内不得执行阻塞操作。
 *
 * 当前实现：仅做 application_milliseconds++，用于运行时间和帧率统计；
 * 图片解析器尚未使用该计数实现静默超时复位。
 */
void AppUartRx_Timer1msCallback(void);

/**
 * @brief  获取应用运行毫秒计数。
 * @param  None.
 * @return 自应用定时器启动后的毫秒计数值。
 * @note   可重入；32 位 Cortex-M3 对齐读写为原子操作。
 *
 * 用途：
 *   - 日志时间戳
 *   - 性能测量
 */
uint32_t AppUartRx_GetMilliseconds(void);

/** @} */

#ifdef __cplusplus
}
#endif

#endif /* APP_UART_RX_H */
