/*
****************************************************************************
* Copyright(C): TY Technical
* FileName    : dev_led.c
* Author      : TY Technical Software Development Team
* Description : 配置LED闪光灯
****************************************************************************
*/
 
#include "dev_led.h"
#include "drv_gpio.h"

#define LED_GPIO_PIN		(8u)



/*
********************************************************************************
* Function Name : led_set
* Description   : LED闪光灯设置
* Paramter      : led_status: led灯状态
* Return        : None
********************************************************************************
*/
void led_set(STATUS led_status)
{
    if (OFF == led_status)
    {		
				gpio_pin_write(LED_GPIO_PIN, PIN_LOW);
    }
    else
    {		
				gpio_pin_write(LED_GPIO_PIN, PIN_HIGH);
    }
}


/*
********************************************************************************
* Function Name : led_init
* Description   : LED闪光灯初始化
* Paramter      : None
* Return        : None
********************************************************************************
*/
void led_init(void)
{
  gpio_cfg_output(LED_GPIO_PIN);  
}


