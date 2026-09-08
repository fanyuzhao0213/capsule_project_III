/*
****************************************************************************
* Copyright(C): TY Technical
* FileName    : do_main.c
* Author      : TY Technical Software Development Team
* Description : /////////
****************************************************************************
*/
#include "do_main.h"
// 驱动头文件
#include "drv_power_mode.h"
#include "drv_usart.h"
#include "drv_wdt.h"
// 应用头文件
#include "jpeg_head.h"
#include "time_handle.h"
#include "system_stm32_tick.h"
#include "receive_send_image.h"
#include "storage_card.h"
#include "delay.h"
#include "usart_transfer_station.h"
#include <string.h>
#include "calendar.h"
#include "drv_sd2058.h"
#include "exti.h"
#include "bsp_TiMbase.h" 

//蜂鸣器响持续时间
#define beep_on_delay_ms 10

//蜂鸣器周期
#define beep_on_cycly_ms 3000

//文件创建标志
BOOL Capsule_File_FLAG = FALSE;



/*
********************************************************************************
* Function Name : driving_program_Init
* Description   : 底层驱动初始化
* Paramter      : None
* Return        : None
********************************************************************************
*/
static void driving_program_Init(void)
{ 
  // 电源
  Power_Mode_GPIOConfig();
	
  //上电
  power_up();
  
//	//初始化蜂鸣器
//	Beep_Init();
	
	// 更新时间 
  switch_on_uptime_init(); 
	
//	//外部中断初始化，相应SD2058频率中断
//	EXTI_GPIO_Init();
	
  // 存储卡(最好在其它中断开启之前完成初始化)
  SD_Card_Open_File();  
	
	// 窗口看门狗(在写SD卡数据之前开启，不然会导致写入失败无法重启)
  Iwdg_init();
	
//	//初始化定时器，控制信号强度报警蜂鸣器时间
//	TIMx_Configuration();
	 
  return ;
}


/*
********************************************************************************
* Function Name : app_program_Init
* Description   : 应用程序初始
* Paramter      : None
* Return        : None
********************************************************************************
*/
static void app_program_Init(void)
{
//  // 新格式文件头数据写入
//  SD_Card_write_file_header(); 
	
	// 串口
  USART3_Configuration();
  USART2_Configuration();
	
  // 图片数组初始化
  jpeg_data_buf_Init();
  
	// 对设备信息进行初始清零
	memset(DeviceInfo, 0, 128);

  return ;
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
	//蜂鸣器持续响时间标志
	u32 beep_time_flag = 0;
	//蜂鸣器初始时间
	u32 beep_on_data_lost_time_init = 0;
	
	u32 beep_on_rssi_low_time_init = 0;
	
//	//上次最大RSSI值
//	u8 Rssi_This_Max_Data;
//	//本次最大RSSI值
//  u8 Rssi_Last_Max_Data = 75;
	
	//最大RSSI值低于阈值次数
	u8 CH_REC_SIGNAL_STRENGTH_LOW_TIME = 0;
	
	//最大RSSI值低于阈值标志
	BOOL CH_REC_SIGNAL_STRENGTH_LOW_FLAGE = FALSE;
	
	//最大RSSI值低于阈值标志
	BOOL BEEP_ON_ALARM_FLAG = FALSE;
	
	//数据丢失变量
	u32 Receive_Image_Data_Bef_Sta = 0;
	
	u32  Receive_Image_Data_End_Sta = 0;
		
//	//胶囊数据连接成功标志
//	BOOL CAPCULE_DATA_FIRST_CONNECT_FLAG = FALSE;

  // 底层驱动初始化
  driving_program_Init();
	
  // 应用程序初始
  app_program_Init();
		
	while(1)
	{
		// 喂狗
		Iwdt_FeedDog();
		
		// MCU实时时间更新
		MCU_uptime_now();
		
//		// 根据PC时间设置RTC时间
//		PC_cmd_bcd_uptime();
		
//		//RTC时间更新
//		RTC_uptime_now();
		
		//数据丢失标志
		Receive_Image_Data_Bef_Sta = Receive_Image_Data_End_Sta;

						
		// 接收完成一幅完整的图片
		if (TRUE == receive_image_data_end())
		{   
			// 更新时间时不接收串口2数据
			Img_process_finsh = 1;
			
			if(Capsule_File_FLAG == FALSE)
			{
				//创建保存数据文件
				Capsule_File_FLAG = TRUE;
				SD_Card_Open_Capsule_File(&DeviceInfo[11]);			
				
        //从 SD 卡中读取RTC更新时间到设备信息存储数组
        Read_Rtc_From_SD_Card();
			}
			
			//RTC时间更新
		  //RTC_uptime_now();
			SD2058_ReadBytes_From_Begin((u8*)&TimeNow, 7);//获得RTC SD2058 时间
			
			Img_process_finsh = 0;
			
			// SD卡存储图片
			SD_Card_storage_imgdata(); 
			
			// 发送图片到PC端
			send_image_to_PC(CMD_IMG_FORWARD); 
					
			// 冲洗缓存
			SD_Card_storage_10_flush(); 
			
			
//			// 通道信号弱报警			
//			if(Rssi_Max_Data(DeviceInfo)>CH_REC_SIGNAL_STRENGTH_LOW_TH)
//			{
//				CH_REC_SIGNAL_STRENGTH_LOW_TIME ++;
//				
//				if(CH_REC_SIGNAL_STRENGTH_LOW_TIME>CH_REC_SIGNAL_STRENGTH_ALA_TH) // 通道RSSI弱报警阈值
//				{  			
//					CH_REC_SIGNAL_STRENGTH_LOW_TIME = 0;
//					CH_REC_SIGNAL_STRENGTH_LOW_FLAGE = TRUE;
//				} 
//				
//			}
//			else
//			{
//				CH_REC_SIGNAL_STRENGTH_LOW_TIME = 0;
//				CH_REC_SIGNAL_STRENGTH_LOW_FLAGE = FALSE;
//			}
			
//			//数据丢失标志
//			Receive_Image_Data_End_Sta++;	
//			
//			beep_on_data_lost_time_init=get_sys_tick_time();

			
					
		}
			
		// 序列号绑定
		capsule_sn_CMDDeal();
		
//////查询报警模式		
		// 进入通道信号强度弱报警

//		//if(((CH_REC_SIGNAL_STRENGTH_LOW_FLAGE)||((Receive_Image_Data_Bef_Sta==Receive_Image_Data_End_Sta)&&(Receive_Image_Data_Bef_Sta))))
//		//if(CH_REC_SIGNAL_STRENGTH_LOW_FLAGE)
//		if((Receive_Image_Data_Bef_Sta==Receive_Image_Data_End_Sta)&&(Receive_Image_Data_Bef_Sta))// 数据丢失报警
//		{
//			if((get_sys_tick_time()-beep_on_data_lost_time_init)>beep_on_cycly_ms)
//			{
//				beep_on_data_lost_time_init = get_sys_tick_time();
//				// 数据丢失报警
//				Beep_On();
//				beep_time_flag = get_sys_tick_time();	
//				BEEP_ON_ALARM_FLAG = TRUE;
//			}
//		}
//		else
//		{
//			if(CH_REC_SIGNAL_STRENGTH_LOW_FLAGE)// 通道信号弱报警
//			{
//				if((get_sys_tick_time()-beep_on_rssi_low_time_init)>beep_on_cycly_ms)
//				{
//					beep_on_rssi_low_time_init = get_sys_tick_time();
//					// 通道信号弱报警
//					Beep_On();
//					beep_time_flag = get_sys_tick_time();	
//					BEEP_ON_ALARM_FLAG = TRUE;
//				}
//		  }
//	  }
		
//		// 关闭通道信号强度报警
//		if(BEEP_ON_ALARM_FLAG)
//		{
//			if((get_sys_tick_time()- beep_time_flag)>beep_on_delay_ms)
//			{
//			 Beep_Off();
//			 beep_time_flag = 0;
//			 BEEP_ON_ALARM_FLAG = FALSE;
//			}
//		}
//////查询报警模式			
		
	} 
}

