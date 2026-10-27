/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : STM32F103RET6 主控应用入口
  *                   ├─ HAL 初始化（时钟 / GPIO / DMA / IWDG / SDIO / FATFS）
  *                   ├─ 应用层初始化（UART2/3 DMA RX + TIM1 1ms 中断 + SD 卡）
  *                   └─ 主循环：UART 帧解析 + 喂狗
  *
  * 数据链路总览：
  *   胶囊（nRF52 TX）──2.4GHz Radio──>  RF 接收板（nRF52 RX）
  *                                              │
  *                                              │  UART2 1Mbps（RF 板 → STM32）
  *                                              ▼
  *                                       本 STM32 主控  ──UART3 921600bit/s──> PC
  *                                              │
  *                                              │  SD 卡 FATFS（Y<SN>.YS）
  *                                              ▼
  *                                          SD 存储
  *
  * 本文件职责：
  *   - 配置 72MHz 系统时钟（HSE 8MHz × PLL9）
  *   - 初始化 HAL 外设（GPIO/DMA/IWDG/TIM/USART/SDIO/FATFS）
  *   - 启动应用层服务（AppLog / AppUartRx / SdStorage）
  *   - 主循环：UART 帧处理 + IWDG 喂狗
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  ******************************************************************************
  */
/* USER CODE END Header */

/* Includes ------------------------------------------------------------------*/
// HAL 与 BSP 标准头
#include "main.h"           // 主头文件（HAL 配置）
#include "dma.h"            // DMA 控制器（USART2/3 RX 用 DMA 搬运）
#include "fatfs.h"          // FatFs 文件系统（SD 卡读写）
#include "iwdg.h"           // 独立看门狗（防止系统死锁）
#include "sdio.h"           // SDIO 接口（SD 卡底层驱动）
#include "tim.h"            // 定时器（TIM1 1ms 中断）
#include "usart.h"          // UART1/2/3 串口驱动
#include "gpio.h"           // GPIO 配置

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
// 应用层模块
#include "app_log.h"        // 日志模块（当前 usart.c 实际配置为 USART1 1Mbit/s 8N1）
#include "app_uart_rx.h"    // UART2/3 DMA RX 帧解析器（与 RF 板通信）
#include "sd_storage.h"     // SD 卡 FATFS 存储（Y<SN>.YS 文件）
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/

/* USER CODE BEGIN PV */

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);    // 系统时钟配置（HSE 8M × PLL9 = 72MHz）
/* USER CODE BEGIN PFP */

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

/* USER CODE END 0 */



/**
  * @brief  应用入口；上电后硬件初始化 → 进入主循环
  * @retval int（实际永不返回）
  *
  * 启动流程详解：
  *   ① HAL_Init()
  *        - 初始化 Flash 接口与 Systick
  *        - 配置 NVIC 优先级分组（4bit 抢占 + 4bit 子优先级）
  *        - 设置 HAL 时间基准（1ms tick）
  *
  *   ② SystemClock_Config()
  *        - HSE 8MHz × PLL9 = 72MHz 系统时钟
  *        - AHB = 72MHz（不分频）
  *        - APB1 = 36MHz（AHB/2，最大 36MHz 限制）
  *        - APB2 = 72MHz（不分频）
  *        - Flash 等待 2 个周期（72MHz 对应）
  *
  *   ③ MX_*_Init()：按以下顺序初始化
  *        - MX_GPIO_Init()         GPIO 复用映射
  *        - MX_DMA_Init()          DMA1/DMA2 控制器
  *        - MX_IWDG_Init()         独立看门狗（典型约3.28s，实际受LSI误差影响）
  *        - MX_TIM1_Init()         TIM1 1ms 周期中断
  *        - MX_USART1_UART_Init()  USART1 = 1Mbit/s 8N1（日志）
  *        - MX_USART2_UART_Init()  USART2 = 1Mbps 8N1（接 RF 板）
  *        - MX_USART3_UART_Init()  USART3 = 921600bit/s 8N1（接PC）
  *        - MX_SDIO_SD_Init()      SDIO + SD 卡底层
  *        - MX_FATFS_Init()        FatFs 文件系统
  *
  *   ④ AppLog_Init() + LOG_INFO
  *        - 初始化 USART1 日志
  *        - 打印 "System start" 启动消息
  *
  *   ⑤ AppUartRx_Init()
  *        - 启动 USART2 / USART3 的 DMA 循环接收
  *        - 失败则进入 Error_Handler（死循环 + 关闭中断）
  *
  *   ⑥ HAL_TIM_Base_Start_IT(&htim1)
  *        - 启动 TIM1 中断（1ms 周期）
  *        - 每次中断调用 AppUartRx_Timer1msCallback()
  *
  *   ⑦ SdStorage_Init()
  *        - 挂载 FATFS 到 SD 卡
  *        - 失败仅打印警告，不阻塞（应用继续运行）
  *
  *   ⑧ while(1) 主循环
  *        - AppUartRx_Process()：处理 UART2/3 接收缓冲中的帧
  *        - HAL_IWDG_Refresh()：喂狗（必须在实际IWDG超时前完成）
  *
  * 注：
  *   - UART3图片发送使用DMA；SD/FatFs写入和部分控制帧转发仍是主循环阻塞操作
  *   - SD写入前后主动喂狗，避免慢卡操作触发复位
  */
/**
 * @brief STM32主入口：初始化外设、协议和存储，然后循环处理双UART数据。
 * @return 嵌入式主循环不会退出，返回值仅满足C语言入口约定。
 *
 * 简易流程图：
 *   复位/HAL初始化
 *       → 72MHz系统时钟
 *       → GPIO/DMA/IWDG/TIM/UART/SDIO/FatFs初始化
 *       → 协议、双UART循环DMA和1ms时基启动
 *       → 挂载SD卡（失败不阻止通信）
 *       → while(1)：消费UART数据 → 协议处理 → 喂狗
 */
int main(void)
{

  /* USER CODE BEGIN 1 */

  /* USER CODE END 1 */

  /* MCU Configuration--------------------------------------------------------*/

  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
  HAL_Init();

  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* 配置系统时钟（HSE 8M × PLL9 = 72MHz） */
  SystemClock_Config();

  /* USER CODE BEGIN SysInit */

  /* USER CODE END SysInit */

  /* 按依赖顺序初始化所有外设 */
  MX_GPIO_Init();
  MX_DMA_Init();
  MX_IWDG_Init();
  MX_TIM1_Init();
  MX_USART1_UART_Init();
  MX_USART2_UART_Init();
  MX_USART3_UART_Init();
  MX_SDIO_SD_Init();
  MX_FATFS_Init();

  /* USER CODE BEGIN 2 */
  // 初始化日志模块。
  AppLog_Init();
  LOG_INFO("System start, USART1 log baud rate: 1Mbit");

  // 初始化 UART2/3 的 DMA 循环接收
  if (AppUartRx_Init() == 0U)
  {
    LOG_ERROR("UART2/UART3 DMA RX initialization failed");
    Error_Handler();  // 初始化失败 → 进入死循环
  }

  // 启动TIM1 1ms周期中断；当前只维护应用毫秒计数，尚未实现图片帧静默超时复位。
  if (HAL_TIM_Base_Start_IT(&htim1) != HAL_OK)
  {
    LOG_ERROR("TIM1 1 ms interrupt start failed");
    Error_Handler();
  }
  LOG_INFO("UART2/UART3 DMA RX and TIM1 started");

  // 初始化 SD 卡存储（挂载 FATFS）
  if (SdStorage_Init() == SD_STORAGE_OK)
  {
    // 成功 → 可选自检（已注释，避免 SD 卡调试卡死）
    // (void)SdStorage_RunSelfTest();
  }
  else
  {
    // 失败仅警告，不阻塞（应用继续运行，SD 相关功能失效）
    LOG_WARNING("SD card unavailable, application continues without storage");
  }
  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
    // 处理 UART2/3 接收缓冲中的完整帧（解析 0xFF 0x55 0x12 0x34 协议）
    AppUartRx_Process();

    // 喂狗：主循环正常运行时持续刷新 IWDG
    // Prescaler=32、Reload=4095；按40kHz典型LSI估算约3.28s。
    (void)HAL_IWDG_Refresh(&hiwdg);
  }
  /* USER CODE END 3 */
}



/**
  * @brief  系统时钟配置（HSE 8MHz × PLL9 = 72MHz）
  * @retval None
  *
  * 时钟树：
  *   HSE 8MHz ─┬─→ PLL ×9 ─→ SYSCLK 72MHz
  *            └─→ （HSE 也可作为 SYSCLK 源，本工程未用）
  *   LSI ~40kHz ─→ IWDG（独立看门狗时钟源）
  *   HSI 8MHz  ─→ （备份，本工程 PLL 锁定后切换到 HSE）
  *
  * 总线分频：
  *   SYSCLK = 72MHz（PLL 输出）
  *   AHB    = SYSCLK / 1 = 72MHz
  *   APB1   = AHB / 2 = 36MHz（最大 36MHz 限制）
  *   APB2   = AHB / 1 = 72MHz
  *
  * Flash 等待：
  *   - 72MHz 对应 FLASH_LATENCY_2（2 个等待周期）
  *   - 错误配置会导致程序跑飞
  *
  * IWDG 时钟源：
  *   - LSI ~40kHz（片内 RC，独立于 HSE，HSE 故障时仍能工作）
  *   - 配置后 IWDG 无法停止（除非系统复位）
  */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  /** 配置 RCC 振荡器（HSE + HSI + LSI + PLL） */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_LSI|RCC_OSCILLATORTYPE_HSE;
  RCC_OscInitStruct.HSEState = RCC_HSE_ON;              // 启用 8MHz 外部晶振
  RCC_OscInitStruct.HSEPredivValue = RCC_HSE_PREDIV_DIV1; // HSE 不分频
  RCC_OscInitStruct.HSIState = RCC_HSI_ON;              // 启用 8MHz 内部 RC（备份）
  RCC_OscInitStruct.LSIState = RCC_LSI_ON;              // 启用 40kHz 内部 RC（IWDG 时钟）
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;          // 启用 PLL
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;  // PLL 输入 = HSE
  RCC_OscInitStruct.PLL.PLLMUL = RCC_PLL_MUL9;          // PLL 倍频 ×9 → 72MHz
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** 配置 CPU / AHB / APB 总线分频 */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;  // SYSCLK = PLL 输出 72MHz
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;          // AHB = 72MHz
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV2;          // APB1 = 36MHz
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;          // APB2 = 72MHz

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_2) != HAL_OK)
  {
    Error_Handler();
  }
}



/* USER CODE BEGIN 4 */



/**
 * @brief  HAL 定时器周期中断回调（1ms 周期，TIM1）
 * @param  htim 触发中断的定时器句柄
 * @return None.
 * @note   由中断上下文调用，不可重入；禁止在此函数中执行阻塞操作
 *
 * 调用链：
 *   TIM1 中断（1ms）
 *     → HAL_TIM_PeriodElapsedCallback()
 *     → AppUartRx_Timer1msCallback()
 *     → 更新 UART 帧超时检测计数
 */
void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim)
{
  if (htim->Instance == TIM1)
  {
    AppUartRx_Timer1msCallback();
  }
}

/* USER CODE END 4 */



/**
  * @brief  HAL 错误处理（中断关闭 + 死循环，等待看门狗复位）
  * @retval None
  *
  * 触发场景：
  *   - 时钟配置失败（HSE 起不来）
  *   - UART 初始化失败
  *   - SD 卡初始化失败（已警告，不调用）
  *   - 其他 HAL 外设错误
  *
  * 处理：
  *   ① __disable_irq()：关闭所有中断，防止错误状态进一步恶化
  *   ② while(1) 空循环：CPU 停止工作，等待 IWDG 超时（典型约3.28s）后系统复位
  *
  * 注：单纯死循环 + 关闭中断会导致无法调试，建议实际项目中添加
  *     错误指示灯 / 日志输出 / 远程告警等。
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  /* User can add his own implementation to report the HAL error return state */
  __disable_irq();
  while (1)
  {
  }
  /* USER CODE END Error_Handler_Debug */
}



#ifdef  USE_FULL_ASSERT
/**
  * @brief  断言失败报告（仅当 USE_FULL_ASSERT 启用时编译）
  * @param  file: 触发断言的源文件名
  * @param  line: 触发断言的源代码行号
  * @retval None
  *
  * 默认实现为空，用户可重写以输出调试信息（通过 printf 或 LOG_ERROR）
  */
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
  /* User can add his own implementation to report the file name and line number,
     ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
