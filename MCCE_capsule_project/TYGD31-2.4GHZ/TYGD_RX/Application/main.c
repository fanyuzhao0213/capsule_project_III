/*
****************************************************************************
* Copyright(C): TY Technology
* FileName    : main.c
* Description : Application main entry
****************************************************************************
*/

#include "system_clock.h"
#include "system_it_vector.h"
#include "driver.h"
#include "antenna.h"
#include "domain.h"



/*
********************************************************************************
* Function Name : main
* Description   : Function for application main entry
* Parameter     : None
* Return        : int return type required by ANSI/ISO standard
********************************************************************************
*/
int main(void)
{
    // 系统初始化
    system_it_vector_init();
    system_clock_init();
	
    // nRF MCU 初始化
    driver_init();
    
    // 应用初始化
    antenna_init();
	
    // 主循环
    domain();
}


