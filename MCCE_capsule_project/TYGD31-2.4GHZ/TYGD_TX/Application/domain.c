/*
****************************************************************************
* Copyright(C): TY Technical
* FileName    : domain.c
* Author      : TY Technical Software Development Team
* Description : 主循环处理
****************************************************************************
*/

#include "domain.h"
#include "image.h"
#include "capsule_sn_broadcast.h"
#include "drv_wdt.h"
#include "drv_radio.h"
#include "drv_timer.h"
#include "drv_spi.h"
#include "dev_led.h"
#include "dev_cx93510.h"
#include "dev_sensor.h"

#include <nrf.h>



/*
********************************************************************************
* Function Name : capsule_init
* Description   : 胶囊上电初始化
* Paramter      : None
* Return        : None
********************************************************************************
*/
static void capsule_init(void)
{	
	// 射频初始化
	radio_init();	
					
	// 驱动初始化
	timer2_init();

	// 看门狗 
	wdt_init();	
	
	// 
	spi_master_init(SPI0);
	
	// 设备初始化
	cx93510_init();
	led_init();
	
}	


/*
********************************************************************************
* Function Name : domain
* Description   : 主循环
* Paramter      : None
* Return        : None
********************************************************************************
*/
void domain(void)
{			
	// 初始化
	capsule_init();

	//胶囊ID号广播
	capsule_id_broadcast_process();
	
	//胶囊序列号设定
	capsule_sn_set_checking();
	
	while(1)
	{
		
		wdt_feed();                  // 喂狗

		__WFI();		// 休眠	

		// 序列号广播
		capsule_sn_broadcast_process();	

		image_process();        // 发送图片及重发
	} 
}


