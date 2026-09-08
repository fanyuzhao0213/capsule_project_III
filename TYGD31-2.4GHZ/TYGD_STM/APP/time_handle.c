/*
****************************************************************************
* Copyright(C): TY Technical
* FileName    : time_handle.c
* Author      : TY Technical Software Development Team
* Description : 时间处理
****************************************************************************
*/
#include "time_handle.h"
#include "calendar.h"
#include "receive_send_image.h"
#include "drv_usart.h"
#include "system_stm32_tick.h"
#include "delay.h"
#include "drv_sd2058.h"
#include <string.h>

// 当前时间
u32 For_time_now = 0;
// 时间信息时间
static u32 Time_info_timer = 0;

// 时间接收完成标志
u8 Rx_time_flinsh = 0;

/*
********************************************************************************
* Function Name  : switch_on_uptime_init
* Description    : 上电更新时间初始化
* Input          : None
* Output         : None
* Return         : None
********************************************************************************
*/
void switch_on_uptime_init(void)
{
	// 获取时钟芯片时间
	SD2058_Init();
  return ;
}


/*
********************************************************************************
* Function Name  : switch_on_uptime
* Description    : 上电更新时间
* Input          : None
* Output         : None
* Return         : None
********************************************************************************
*/
void switch_on_uptime(void)
{
  return ;
}

/*
********************************************************************************
* Function Name  : RTC_time_info
* Description    : RTC的时间信息
* Input          : None
* Output         : None
* Return         : None
********************************************************************************
*/
void RTC_uptime_now(void)
{	 
  GetBcdTime(&TimeNow);//获得RTC SD2058 时间
  return ;
}


/*
********************************************************************************
* Function Name  : MCU_time_info
* Description    : MCU的时间信息
* Input          : None
* Output         : None
* Return         : None
********************************************************************************
*/
void MCU_uptime_now(void)
{	  
	// 每秒钟更新时间显示
  if ((get_sys_tick_time() - Time_info_timer) > 997)
  {
    Time_info_timer = get_sys_tick_time();
    For_time_now++;
  }
  return ;
}


/*
********************************************************************************
* Function Name  : PC_cmd_uptime
* Description    : PC端命令更新时间
* Input          : None
* Output         : None
* Return         : None
********************************************************************************
*/
void PC_cmd_uptime(void)
{ 
  u32 up_time = 0; 
  u8 i;
  u32 Rec_set_time = 0; // 接收设置时间
  
//  if (!Rx_time_flinsh)
//  {
//    return ;
//  }
	
	if (Rx_time_flinsh)//根据pc命令设置rtc sd2058时间
	{

			// 更新时间时不接收串口2数据
			Img_process_finsh = 1;
			
			// 重置串口2接收状态
			rx_step2 = 0;
			
			// 为下一次更新时间准备
			Rx_time_flinsh = 0;
			
			// 从上位机获取当前电脑时间并处理
			for (i=0; i<4; i++)
			{
				up_time = (u32)Usart1_rec_buf[i];
				up_time = up_time<<(i*8);
				Rec_set_time += up_time;
			}
				
			// 设置从PC端获取的时间
			SetTime0(Rec_set_time);
			
		//  // 更新设置的时间
		//  switch_on_uptime();
					
			// 对Usart1_rec_buf清零
			memset(Usart1_rec_buf, 0, sizeof(Usart1_rec_buf));
			
			// 
			Img_process_finsh = 0;
  }
	
	  // 更新设置的时间
  switch_on_uptime();
	
  return ;
}


/*
********************************************************************************
* Function Name  : PC_cmd_bcd_uptime
* Description    : PC端命bcd码更新RTC时间
* Input          : None
* Output         : None
* Return         : None
********************************************************************************
*/
void PC_cmd_bcd_uptime(void)
{ 
	if (Rx_time_flinsh)//根据pc命令设置rtc sd2058时间
	{

			// 更新时间时不接收串口2数据
			Img_process_finsh = 1;
			
			// 重置串口2接收状态
			rx_step2 = 0;
			
			// 为下一次更新时间准备
			Rx_time_flinsh = 0;
			
			// 从上位机获取当前电脑时间,设置RTC时间
			BcdSetRtcTime(Usart1_rec_buf);
							
			// 对Usart1_rec_buf清零
			memset(Usart1_rec_buf, 0, sizeof(Usart1_rec_buf));
			
			// 
			Img_process_finsh = 0;
  }
  return ;
}

