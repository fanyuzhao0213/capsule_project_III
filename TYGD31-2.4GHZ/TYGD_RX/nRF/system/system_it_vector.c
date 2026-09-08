/*
****************************************************************************
* Copyright(C): TY Technology
* FileName    : system_it_vector.c
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
* Parameter     : None
* Return        : None
********************************************************************************
*/
void system_it_vector_init(void)
{
    SCB->VTOR = VECTOR_TABLE_OFFSET;

    __enable_irq();
}


