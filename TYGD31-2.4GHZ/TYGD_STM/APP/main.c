/*
****************************************************************************
* Copyright(C): TY Technical
* FileName    : main.c
* Author      : TY Technical Software Development Team
* Description : Application main entry
****************************************************************************
*/
#include "system_stm32_vector.h"
#include "system_stm32_rcc.h"
#include "system_stm32_tick.h"
#include "do_main.h"



/*
********************************************************************************
* Function Name : main
* Description   : Function for application main entry
* Paramter      : None
* Return        : int return type required by ANSI/ISO standard
********************************************************************************
*/
int main(void)
{
	// 配置中断优先级别组 (4/4 抢占优先级4，副优先级4)
  NVIC_PriorityGroupConfig(NVIC_PriorityGroup_2);	
	
  // 系统初始化
  system_vector_init();
  system_tick_init(); 
	 
  domain();
}
