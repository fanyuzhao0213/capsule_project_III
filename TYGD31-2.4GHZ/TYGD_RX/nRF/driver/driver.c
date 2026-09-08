/*
****************************************************************************
* Copyright(C): TY Technology
* FileName    : driver.c
* Description : nRF驱动配置
****************************************************************************
*/

#include "driver.h"
#include "drv_systick.h"
#include "drv_wdt.h"
#include "drv_uart.h"
#include "drv_timer.h"
#include "drv_radio.h"


/*
********************************************************************************
* Function Name : driver_init
* Description   : nRF MCU 初始化
* Parameter     : None
* Return        : None
********************************************************************************
*/
void driver_init(void)
{
		systick_init();	
	  delay_ms(50);  // 等待电源稳定
    
    timer1_init();
		radio_init();
		uart_init();

		wdt_init();
}


