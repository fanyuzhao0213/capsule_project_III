/*
****************************************************************************
* Copyright(C): TY Technical
* FileName    : exti.c
* Author      : TY Technical Software Development Team
* Description : 外部中断
****************************************************************************
*/
#include "exti.h"
#include "calendar.h"
#include "drv_sd2058.h"

/*
********************************************************************************
* Function Name  : EXTI_GPIO_Init
* Description    : GPIOB.5 中断线以及中断初始化配置
* Input          : None
* Output         : None
* Return         : None
********************************************************************************
*/
void EXTI_GPIO_Init(void)
{
	NVIC_InitTypeDef NVIC_InitStructure;
	EXTI_InitTypeDef EXTI_InitStructure;
	GPIO_InitTypeDef GPIO_InitStructure; 
	
	RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOB|RCC_APB2Periph_AFIO, ENABLE);//打开GPIO AFIO的时钟
	GPIO_InitStructure.GPIO_Pin = GPIO_Pin_5;
	GPIO_InitStructure.GPIO_Speed = GPIO_Speed_10MHz;
	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IPD;
	GPIO_Init(GPIOB, &GPIO_InitStructure); 
		
	EXTI_ClearITPendingBit(EXTI_Line5);
	GPIO_EXTILineConfig(GPIO_PortSourceGPIOB,GPIO_PinSource5);//PB5  为GPIOB的PIN5
	EXTI_InitStructure.EXTI_Line= EXTI_Line5; //PB5,为:EXTI_Line5
	EXTI_InitStructure.EXTI_Mode= EXTI_Mode_Interrupt; 
	EXTI_InitStructure.EXTI_Trigger= EXTI_Trigger_Falling;   //下降沿中断
	EXTI_InitStructure.EXTI_LineCmd=ENABLE;
	EXTI_Init(&EXTI_InitStructure);
		
	NVIC_InitStructure.NVIC_IRQChannel = EXTI9_5_IRQn;
	NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority= 3;
	NVIC_InitStructure.NVIC_IRQChannelSubPriority= 0;        
	NVIC_InitStructure.NVIC_IRQChannelCmd=ENABLE;   
	NVIC_Init(&NVIC_InitStructure);	
}

/*
********************************************************************************
* Function Name  : EXTI_GPIO_Init
* Description    : GPIOB.5 中断响应函数，相应SD2058频率中断
* Input          : None
* Output         : None
* Return         : None
********************************************************************************
*/
void EXTI9_5_IRQHandler(void)			//EXTI9_5 (外部中断号9~5响应函数)
{		
	 if(EXTI_GetITStatus(EXTI_Line5) != RESET)	//判断相应中断号是否进入中断
	 {
		 EXTI_ClearITPendingBit(EXTI_Line5);		//清中断
		 SD2058_ReadBytes_From_Begin((u8*)&TimeNow, 7);//获得RTC SD2058 时间
   }
}

