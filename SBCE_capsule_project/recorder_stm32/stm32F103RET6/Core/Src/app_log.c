/**
 ******************************************************************************
 * @file    app_log.c
 * @copyright Copyright (c) 2026
 * @author  Capsule Project Team
 * @brief   USART1 分级日志及 printf 重定向实现。
 * @version 1.0.0
 ******************************************************************************
 */

#include "app_log.h"
#include "usart.h"
#include <stdio.h>

#define APP_LOG_TX_TIMEOUT_MS  1000U

/**
 * @brief  初始化日志模块。
 * @param  None.
 * @return None.
 * @note   不可重入；应在 USART1 初始化完成后调用。
 */
void AppLog_Init(void)
{
  /* USART1 已由 CubeMX 初始化，此处保留统一的应用层初始化入口。 */
}

/**
 * @brief  通过 USART1 原样输出一段数据。
 * @param  data 待发送数据指针。
 * @param  length 待发送字节数。
 * @return None.
 * @note   非线程安全，采用阻塞发送；不得在中断服务函数中调用。
 */
void AppLog_Write(const uint8_t *data, uint16_t length)
{
#if APP_LOG_ENABLE
  if ((data != 0) && (length > 0U))
  {
    (void)HAL_UART_Transmit(&huart1, (uint8_t *)data, length,
                            APP_LOG_TX_TIMEOUT_MS); /* HAL 接口未使用 const，发送过程不会修改数据。 */
  }
#else
  (void)data;
  (void)length;
#endif
}

/**
 * @brief  printf 使用的底层单字符输出接口。
 * @param  character 待发送字符。
 * @return 成功时返回原字符，失败时返回 EOF。
 * @note   非线程安全，采用阻塞发送；不得在中断服务函数中调用。
 */
int __io_putchar(int character)
{
  uint8_t data = (uint8_t)character;

#if APP_LOG_ENABLE
  if (HAL_UART_Transmit(&huart1, &data, 1U, APP_LOG_TX_TIMEOUT_MS) != HAL_OK)
  {
    return EOF;
  }
#endif
  return character;
}

/**
 * @brief  将 ARMCC 标准库的 fputc 接口重定向到 USART1。
 * @param  character 待发送字符。
 * @param  stream 标准库流指针，本实现不使用。
 * @return 成功时返回原字符，失败时返回 EOF。
 * @note   非线程安全；printf 最终逐字符调用本函数和 __io_putchar()。
 */
int fputc(int character, FILE *stream)
{
  (void)stream;
  return __io_putchar(character);
}
