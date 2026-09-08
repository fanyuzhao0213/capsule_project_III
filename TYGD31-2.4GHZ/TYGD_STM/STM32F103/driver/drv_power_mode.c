/*
****************************************************************************
* Copyright(C): TY Technical
* FileName    : drv_power_mode.c
* Author      : TY Technical Software Development Team
* Description : 配置主控板电源模式
****************************************************************************
*/
#include "drv_power_mode.h"

// 引脚定义  
#define SDOE_USB    PCout(1)


/*
********************************************************************************
* Function Name  : Power_Mode_GPIOConfig
* Description    : 电源模式管脚初始化
* Input          : None
* Output         : None
* Return         : None
********************************************************************************
*/
void Power_Mode_GPIOConfig(void)
{
    GPIO_InitTypeDef GPIO_InitStructure;

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOC | RCC_APB2Periph_AFIO, ENABLE);  //
	
    // SDOE_USB PC1
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_1; 
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_Out_PP;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_2MHz;
    GPIO_Init(GPIOC, &GPIO_InitStructure); 
		
    return ;
}


/*
********************************************************************************
* Function Name  : power_up
* Description    : 上电配置
* Input          : None
* Output         : None
* Return         : None
********************************************************************************
*/
void power_up(void)
{
    // 主控板进入MCU模式
    SDOE_USB = USB_MCU; 
  
    return ;
}


/*
********************************************************************************
* Function Name  : power_up
* Description    : 断电模式
* Input          : None
* Output         : None
* Return         : None
********************************************************************************
*/
void power_down(void)
{		    
	// 主控板进入U盘模式
  SDOE_USB = USB_AU;     
  
	return ;
}


/*
********************************************************************************
* Function Name  : Beep_Init
* Description    : Beep管脚初始化
* Input          : None
* Output         : None
* Return         : None
********************************************************************************
*/
void Beep_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStructure;

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOC, ENABLE);  //
	
    //  设置PC4为蜂鸣器控制脚
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_4; 
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_Out_PP;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_2MHz;
    GPIO_Init(GPIOC, &GPIO_InitStructure); 
		GPIO_ResetBits(GPIOC,GPIO_Pin_4);
    return ;
}


/*
********************************************************************************
* Function Name  : Beep_Toggle
* Description    : Beep 切换
* Input          : state 蜂鸣器状态
* Output         : None
* Return         : None
********************************************************************************
*/
void Beep_Toggle(BOOL state)
{ 
	if(state)
		GPIO_SetBits(GPIOC,GPIO_Pin_4);
	else
		GPIO_ResetBits(GPIOC,GPIO_Pin_4);
    return ;
}



/*
********************************************************************************
* Function Name  : Beep_On
* Description    : Beep 开
* Input          : None
* Output         : None
* Return         : None
********************************************************************************
*/
void Beep_On(void)
{ 
	GPIO_SetBits(GPIOC,GPIO_Pin_4);
}



/*
********************************************************************************
* Function Name  : Beep_On
* Description    : Beep 关
* Input          : None
* Output         : None
* Return         : None
********************************************************************************
*/
void Beep_Off(void)
{ 
	GPIO_ResetBits(GPIOC,GPIO_Pin_4);
}



/*
********************************************************************************
* Function Name  : Rssi_Max_Data
* Description    : 返回RSSI最大值
* Input          : buff_info
* Output         : None
* Return         : RSSI最大值
********************************************************************************
*/
u8 Rssi_Max_Data(u8 *buff_info)
{ 
	u8 rssi_data;
	
	//选出最大rssi值
	switch(buff_info[49])
	{
		case 1:
			rssi_data = buff_info[39];
		  break;
		case 2:
			rssi_data = buff_info[40];
		  break;
		case 3:
			rssi_data = buff_info[41];
		  break;
		case 4:
			rssi_data = buff_info[42];
		  break;
		case 5: 
			rssi_data = buff_info[43];
		  break;
		case 6:
			rssi_data = buff_info[44];
		  break;
		case 7:
			rssi_data = buff_info[51];
		  break;
		case 8:
			rssi_data = buff_info[52];
		  break;
		case 9:
			rssi_data = buff_info[53];
		  break;
		case 10:
			rssi_data = buff_info[54];
		  break;
		case 11: 
			rssi_data = buff_info[55];
		  break;
		case 12:
			rssi_data = buff_info[56];
		  break;	
	}
	
  return rssi_data;

}




