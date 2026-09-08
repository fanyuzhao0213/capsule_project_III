/*
****************************************************************************
* Copyright(C): TY Technical
* FileName    : system_it_vector.c
* Author      : TY Technical Software Development Team
* Description : 配置系统中断向量
****************************************************************************
*/

#include "system_it_vector.h"
#include "nrf.h"



// 中断向量表偏移地址
#define VECTOR_TABLE_OFFSET          (FLASH_APP_ADDR)



/*
********************************************************************************
* Function Name : system_it_vector_init
* Description   : 初始化系统中断向量表
* Paramter      : None
* Return        : None
********************************************************************************
*/
void system_it_vector_init(void)
{
    SCB->VTOR = VECTOR_TABLE_OFFSET;

    __enable_irq();
}


