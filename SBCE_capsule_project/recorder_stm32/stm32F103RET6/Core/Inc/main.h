/**
 ******************************************************************************
 * @file    main.h
 * @brief   STM32F103RET6 应用主头文件
 * @version 1.0.0
 *
 * 包含 HAL 生成的外设句柄 extern 声明 + 应用层模块头。
 * 用户可在 USER CODE 区追加自定义声明。
 ******************************************************************************
 */

#ifndef __MAIN_H
#define __MAIN_H

#ifdef __cplusplus
extern "C" {
#endif

#include "stm32f1xx_hal.h"

/* USER CODE BEGIN Includes */
#include "app_log.h"        // 日志模块
#include "app_uart_rx.h"    // UART2/3 DMA 接收
#include "capsule_protocol.h"  // 协议解析
#include "sd_storage.h"     // SD 卡存储
#include "ring_buffer.h"    // 环形缓冲
/* USER CODE END Includes */

/* Exported functions prototypes ---------------------------------------------*/
/* USER CODE BEGIN EFP */
/* USER CODE END EFP */

/* HAL 生成的全局句柄（在 .c 文件中定义） */
extern TIM_HandleTypeDef  htim1;       // TIM1 1ms 周期定时器
extern UART_HandleTypeDef huart1;      // USART1 = 日志输出（115200 8N1）
extern UART_HandleTypeDef huart2;      // USART2 = 接 nRF 接收板（1Mbps 8N1）
extern UART_HandleTypeDef huart3;      // USART3 = 接 PC（921600 bit/s，8N1）
extern DMA_HandleTypeDef  hdma_usart2_rx;   // USART2 RX DMA
extern DMA_HandleTypeDef  hdma_usart3_rx;   // USART3 RX DMA
extern SD_HandleTypeDef   hsd;         // SD 卡 SDIO 接口
extern IWDG_HandleTypeDef hiwdg;       // 独立看门狗

/* USER CODE BEGIN Private defines */
/* USER CODE END Private defines */

void SystemClock_Config(void);
void Error_Handler(void);

#ifdef __cplusplus
}
#endif

#endif /* __MAIN_H */
