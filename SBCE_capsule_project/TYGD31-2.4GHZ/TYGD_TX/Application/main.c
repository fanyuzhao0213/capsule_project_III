/*
****************************************************************************
* Copyright(C): TY Technical
* FileName    : main.c
* Author      : TY Technical Software Development Team
* Description : Application main entry
****************************************************************************
*/

#include "system_clock.h"
#include "system_it_vector.h"
#include "domain.h"
#include "nrf52.h"


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
    // 系统初始化
    system_it_vector_init();
    system_clock_init();      	
		
    // 主循环
    domain();
}


