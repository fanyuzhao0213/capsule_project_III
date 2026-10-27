/**
 ******************************************************************************
 * @file    app_log.h
 * @copyright Copyright (c) 2026
 * @author  Capsule Project Team
 * @brief   USART1 分级日志及 printf 重定向配置接口。
 * @version 1.0.0
 ******************************************************************************
 */

#ifndef APP_LOG_H
#define APP_LOG_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

/** @brief 编译期日志总开关：设为 0 可移除全部日志。 */
#ifndef APP_LOG_ENABLE
#define APP_LOG_ENABLE              1
#endif

/** @brief 日志等级定义。 */
#define APP_LOG_LEVEL_ERROR         1
#define APP_LOG_LEVEL_WARNING       2
#define APP_LOG_LEVEL_INFO          3
#define APP_LOG_LEVEL_DEBUG         4

/** @brief 编译期日志过滤等级。 */
#ifndef APP_LOG_LEVEL
#define APP_LOG_LEVEL               APP_LOG_LEVEL_DEBUG
#endif

#if APP_LOG_ENABLE
#include <stdio.h>
#define LOG_ERROR(format, ...)   do { if (APP_LOG_LEVEL >= APP_LOG_LEVEL_ERROR)   { printf("[E] " format "\r\n", ##__VA_ARGS__); } } while (0)
#define LOG_WARNING(format, ...) do { if (APP_LOG_LEVEL >= APP_LOG_LEVEL_WARNING) { printf("[W] " format "\r\n", ##__VA_ARGS__); } } while (0)
#define LOG_INFO(format, ...)    do { if (APP_LOG_LEVEL >= APP_LOG_LEVEL_INFO)    { printf("[I] " format "\r\n", ##__VA_ARGS__); } } while (0)
#define LOG_DEBUG(format, ...)   do { if (APP_LOG_LEVEL >= APP_LOG_LEVEL_DEBUG)   { printf("[D] " format "\r\n", ##__VA_ARGS__); } } while (0)
#define LOG_RAW(data, length)    AppLog_Write((data), (length))
#else
#define LOG_ERROR(format, ...)   ((void)0)
#define LOG_WARNING(format, ...) ((void)0)
#define LOG_INFO(format, ...)    ((void)0)
#define LOG_DEBUG(format, ...)   ((void)0)
#define LOG_RAW(data, length)    ((void)0)
#endif

/**
 * @brief  初始化日志模块。
 * @param  None.
 * @return None.
 * @note   不可重入；应在 USART1 初始化完成后调用。
 */
void AppLog_Init(void);

/**
 * @brief  通过 USART1 原样输出一段数据。
 * @param  data 待发送数据指针。
 * @param  length 待发送字节数。
 * @return None.
 * @note   非线程安全，采用阻塞发送；不得在中断服务函数中调用。
 */
void AppLog_Write(const uint8_t *data, uint16_t length);

/**
 * @brief  printf 使用的底层单字符输出接口。
 * @param  character 待发送字符。
 * @return 成功时返回原字符，失败时返回 EOF。
 * @note   非线程安全，采用阻塞发送；不得在中断服务函数中调用。
 */
int __io_putchar(int character);

#ifdef __cplusplus
}
#endif

#endif /* APP_LOG_H */
