/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    iwdg.c
  * @brief   This file provides code for the configuration
  *          of the IWDG instances.
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */
/* Includes ------------------------------------------------------------------*/
#include "iwdg.h"

/* USER CODE BEGIN 0 */

/* USER CODE END 0 */

IWDG_HandleTypeDef hiwdg;

/**
 * @brief 初始化独立看门狗；启动后只能通过系统复位停止。
 *
 * 简易流程图：
 *   选择LSI时钟
 *       → 预分频32
 *       → 重装值4095
 *       → HAL_IWDG_Init启动计数
 *       → 主循环及SD长操作周期调用HAL_IWDG_Refresh
 *
 * @note 按40kHz典型LSI估算超时约(4096×32)/40000=3.28s；
 *       LSI误差较大，不能把3.28s当成精密定时值。
 */
void MX_IWDG_Init(void)
{

  /* USER CODE BEGIN IWDG_Init 0 */

  /* USER CODE END IWDG_Init 0 */

  /* USER CODE BEGIN IWDG_Init 1 */

  /* USER CODE END IWDG_Init 1 */
  hiwdg.Instance = IWDG;
  /* Give slow SD cards enough time for cluster allocation and a 20 KB write. */
  hiwdg.Init.Prescaler = IWDG_PRESCALER_32;
  hiwdg.Init.Reload = 4095;
  if (HAL_IWDG_Init(&hiwdg) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN IWDG_Init 2 */
  /* HAL_IWDG_Init()返回后看门狗已开始计数，后续必须周期刷新。 */
  /* USER CODE END IWDG_Init 2 */

}

/* USER CODE BEGIN 1 */
/*
 * main()每轮刷新IWDG；SD写入等长操作也会在开始和结束处主动刷新。
 * 如果主循环或阻塞操作超过实际超时时间，MCU会由IWDG复位。
 */
/* USER CODE END 1 */
