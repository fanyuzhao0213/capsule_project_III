/**
 ******************************************************************************
 * @file    capsule_protocol.h
 * @brief   TYGD31 胶囊图像串口协议流式解析接口。
 * @version 1.0.0
 ******************************************************************************
 */

#ifndef CAPSULE_PROTOCOL_H
#define CAPSULE_PROTOCOL_H

#include <stdint.h>

/**
 * @brief  初始化协议解析器状态。
 * @param  None.
 * @return None.
 * @note   不可重入；在 UART DMA 接收启动后调用一次。
 */
void CapsuleProtocol_Init(void);

/** Report deferred USART3 DMA completion/errors from main-loop context. */
void CapsuleProtocol_Process(void);

/**
 * @brief  向协议解析器提交来自 nRF 接收板的数据。
 * @param  data 输入字节流。
 * @param  length 输入长度。
 * @return None.
 * @note   非线程安全；只允许在主循环中调用，支持任意 DMA 分段边界。
 */
void CapsuleProtocol_InputFromNrf(const uint8_t *data, uint16_t length);

/**
 * @brief  把电脑端命令透明转发给 nRF 接收板。
 * @param  data 输入命令数据。
 * @param  length 输入长度。
 * @return None.
 * @note   使用阻塞 UART 发送，只允许在主循环中调用。
 */
void CapsuleProtocol_InputFromPc(const uint8_t *data, uint16_t length);

#endif /* CAPSULE_PROTOCOL_H */
