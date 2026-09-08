/*
****************************************************************************
* Copyright(C): TY Technical
* FileName    : main.c
* Author      : TY Technical Software Development Team
* Description : STM32 主控应用入口；系统启动 → 中断配置 → 时钟初始化 → 进入主循环
*
* 数据链路总览（与本工程相关的角色）：
*   胶囊（nRF52 TX）  ──2.4GHz Radio──>  RF 接收板（nRF52 RX）
*                                              │
*                                              │  UART3 1Mbps
*                                              ▼
*                                       本 STM32 主控  ──UART3──>  PC / U 盘
*                                              │
*                                              │  SD 卡 FATFS
*                                              ▼
*                                          Y<SN>.YS 文件
*
* 本文件职责：
*   1. 配置 NVIC 中断优先级分组
*   2. 调用 system_vector_init()   → 中断向量表 / SysTick 初始化
*   3. 调用 system_tick_init()      → 系统滴答定时器初始化
*   4. 进入 domain()                → 主循环（永不返回）
****************************************************************************
*/
#include "system_stm32_vector.h"   // 中断向量与系统初始化
#include "system_stm32_rcc.h"       // 系统时钟配置（当前未直接使用）
#include "system_stm32_tick.h"      // SysTick 滴答定时器
#include "do_main.h"                // 应用层主循环入口



/*
********************************************************************************
* Function Name : main
* Description   : STM32 主控应用入口；上电后硬件初始化 → 进入主循环
* Paramter      : None
* Return        : int 满足 ANSI/ISO 标准返回类型（实际永不返回）
*
* 启动流程：
*   ① NVIC_PriorityGroupConfig
*        - 设置 4bit 抢占优先级 + 4bit 子优先级（Group_2）
*        - 后续所有中断优先级配置都基于此分组
*
*   ② system_vector_init()
*        - 设置中断向量表位置
*        - 初始化 SysTick（1ms 周期，用于 HAL_Delay / 时间戳）
*
*   ③ system_tick_init()
*        - 配置系统滴答定时器，提供 get_sys_tick_time() 接口
*        - 后续 do_main 中的超时判断都依赖此 tick
*
*   ④ domain()
*        - 应用层主循环入口（参见 do_main.c）
*        - 包含：驱动初始化、应用初始化、图像接收、SD 存储、UART3 转发
*        - 永不返回（while(1) 循环 + 喂狗）
********************************************************************************
*/
int main(void)
{
	  // ─── ① NVIC 优先级分组 ─────────────────────────────────────
	  // Group_2：4bit 抢占优先级 + 4bit 子优先级（共 4 级抢占 + 4 级子优先级）
	  //   - Radio 中断：优先级 5
	  //   - UART 中断：优先级 5
	  //   - SysTick（系统滴答）：最低优先级
	  //   - 喂狗、看门狗：最高优先级
	  NVIC_PriorityGroupConfig(NVIC_PriorityGroup_2);

	  // ─── ② 中断向量表初始化 ────────────────────────────────────
	  // 设置中断向量表位置，配置 SysTick 1ms 中断
	  system_vector_init();

	  // ─── ③ 系统滴答定时器初始化 ─────────────────────────────────
	  // 启动 SysTick，提供毫秒级时间基准
	  // 后续 get_sys_tick_time() / 各种超时都依赖它
	  system_tick_init();

	  // ─── ④ 进入应用层主循环 ────────────────────────────────────
	  // domain() 内含：
	  //   - 底层驱动初始化（SD 卡 / 看门狗 / 电源 / UART）
	  //   - 应用层初始化（UART 配置 / JPEG 缓冲初始化 / 设备信息清零）
	  //   - while(1) 主循环：
	  //       喂狗 → 时间更新 → 图像接收 → SD 存储 → UART3 转发 → SN 绑定
	  //   - 永不返回
	  domain();
}
