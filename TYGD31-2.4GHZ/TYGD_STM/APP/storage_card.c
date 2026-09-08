/*
****************************************************************************
* Copyright(C): TY Technical
* FileName    : storage_card.c
* Author      : TY Technical Software Development Team
* Description : 存储卡存储处理
****************************************************************************
*/
#include "storage_card.h"
#include "jpeg_head.h"
#include "time_handle.h"
#include "receive_send_image.h"
#include "drv_wdt.h"
#include "system_stm32_tick.h"
#include "stm3210e_eval_sdio_sd.h"	
#include "system_stm32_tick.h"
#include <stdio.h>
#include <string.h>
#include "calendar.h"
#include "drv_usart.h"
#include "drv_power_mode.h"

//#define fre_sect_num 14680064ul //tf卡剩余7GB对应扇区数，每个扇区512个字节
//#define fre_sect_num 2097152ul //tf卡剩余1GB对应扇区数，每个扇区512个字节
#define fre_sect_num 10485760ul //tf卡剩余5GB对应扇区数，每个扇区512个字节
//蜂鸣器响持续时间
#define tf_alarm_beep_on_delay_ms 10
//蜂鸣器周期
#define tf_alarm_beep_on_cycly_ms 2000

//trc 更新时间存放地址
#define RTC_Uptime_Adr 83

//存放更新时间buffer
u8 Rtc_Uptime_buf[7] = {0};


// 文件系统函数返回值
FRESULT Sd_fr;

// 文件系统对象
FATFS Sd_fs;

// 文件目录对象
DIR Sd_dir;

// 文件对象
FIL Sd_f0;

// 文件系统字节类型
UINT Byte_n;

// 图片长度超范围标志
u8 Frame_out_range = 0;

u8 SD_data_buf[20064] __attribute__((aligned(4)));


// 获取剩余容量
DWORD Getfree_Storage = 0x00;

// 图像文件头格式  (__attribute__字节对齐)
const u8 fileHeaderBuff[FILE_HEADER_BUFF_SIZE] __attribute__((aligned(4)))=            // 添加字节对齐
{
    'D' , 'S' , 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,                        // 字符串标记
    0xFF, 0x55, 0x12, 0x34, 0xAB, 0xDC,                                                // 文件头标志
    0x02,                                                                              // 文件格式版本号 V2.0
    (u8)FRAME_SIZE, (u8)(FRAME_SIZE >> 8), (u8)(FRAME_SIZE >> 16),                     // 帧长度
    (u8)FRAME_HEADER_SIZE, (u8)(FRAME_HEADER_SIZE >> 8),                               // 帧头长度
    0x00,                                                                              // 胶囊类型，肠胶囊0x00，胃胶囊0x01
    (u8)FILE_HEADER_SIZE, (u8)(FILE_HEADER_SIZE >> 8), (u8)(FILE_HEADER_SIZE >> 16),   // 文件头长度
};


/*
********************************************************************************
* Function Name  : SD_Card_Open_File
* Description    : 初始化SD_card芯片已经打开文件TY.JDF(如果不存在则创建)
* Input          : None
* Output         : None
* Return         : None
********************************************************************************
*/
void SD_Card_Open_File(void)
{
  u8 i;
  for (i=0; i<3; i++)
  {
      // 包含SD卡初始化
      Sd_fr = f_mount(&Sd_fs, "0:/", 1);
      if (Sd_fr != FR_OK) {} 
      Sd_fr = f_opendir(&Sd_dir, "0:/"); 
      if (Sd_fr != FR_OK) {} 
				
//			//tf卡容量不足报警
//			Alarm_Card_Free_Capacity();	
			
//			Sd_fr = f_open(&Sd_f0, "0:default.JDF", FA_OPEN_ALWAYS | FA_WRITE); 		
//			Sd_fr = f_lseek(&Sd_f0, Sd_f0.fsize);
												
      if(Sd_fr == FR_OK)
      {
        return ;
      }
      
      delay_ms(500);  // 延时500ms
   }
   if(Sd_fr!=FR_OK)
   {
      NVIC_SystemReset();
   }
    
   return ;
}


/*
********************************************************************************
* Function Name  : SD_Card_write_data
* Description    : SD卡写数据函数
* Input          : array 写入数据指针
*                  wr_len 写入数据长度
* Output         : None
* Return         : None
********************************************************************************
*/
static void SD_Card_write_data(u8 *array, u16 wr_len)
{
  // 限制文件大小
  if (Sd_f0.fsize < 0x75B81BC0)		
  {
    Sd_fr = f_lseek(&Sd_f0, Sd_f0.fsize);	
		Sd_fr = f_write(&Sd_f0, array, wr_len, &Byte_n);	
  }


  return ;
}


/*
********************************************************************************
* Function Name  : SD_Card_write_head
* Description    : SD卡写如头文件数据
* Input          : array 写入数据指针
*                  wr_len 写入数据长度
* Output         : None
* Return         : None
********************************************************************************
*/
static void SD_Card_write_head(u8 *array, u16 wr_len)
{
	Sd_fr = f_lseek(&Sd_f0, Sd_f0.fsize);	
	Sd_fr = f_write(&Sd_f0, array, wr_len, &Byte_n);	

  return ;
}


/*
********************************************************************************
* Function Name  : SD_Card_flush
* Description    : 冲洗缓存,使数据真正存储到存储芯片(掉电不丢失)
* Input          : None
* Output         : None
* Return         : None
********************************************************************************
*/
static void SD_Card_flush(void)
{
  Sd_fr = f_sync(&Sd_f0);
  
  // 如果冲洗失败,重新初始化存储卡
	if(Sd_fr != FR_OK)
	{
		//SD_Card_Open_File();
    SD_Card_Open_Capsule_File(&DeviceInfo[11]);		
	}
  
	return ;
}


/*
********************************************************************************
* Function Name  : SD_Card_write_file_header
* Description    : 新格式写入文件头数据
* Input          : None
* Output         : None
* Return         : None
********************************************************************************
*/
void SD_Card_write_file_header(void)
{
  u32 i;
	
  // 有些慢的存储卡可能写完缓存马上冲洗会有问题
  delay_ms(10);
  Iwdt_FeedDog();
	
  // 判断是否已经写入了头数据   
  if (Sd_f0.fsize < FILE_HEADER_SIZE)
  {			
    // 文件创建开始写入200k的数据 
    for (i = 0; i < (FILE_HEADER_SIZE / FILE_HEADER_BUFF_SIZE); i++)
    {
       SD_Card_write_head((u8*)fileHeaderBuff, FILE_HEADER_BUFF_SIZE);
			 Iwdt_FeedDog();
			 delay_ms(5);
    }
		
		delay_ms(10);
		
    SD_Card_flush();
  }
	
  return ;
}


/*
********************************************************************************
* Function Name  : SD_Card_storage_imgdata
* Description    : SD卡存储图片数据
* Input          : None
* Output         : None
* Return         : None
********************************************************************************
*/
void SD_Card_storage_imgdata(void)
{
  u8 i;	
	
  // 获取MCU当前时间
  DeviceInfo[45]=For_time_now;
  DeviceInfo[46]=For_time_now>>8;
  DeviceInfo[47]=For_time_now>>16;	
	
	// 写图像数据长度
	DeviceInfo[65] = Send_data_len;
	DeviceInfo[66] = Send_data_len>>8;
	DeviceInfo[67] = Send_data_len>>16;
	
	// 写RTC SD2058时间
	DeviceInfo[76] = TimeNow.sec;
	DeviceInfo[75] = TimeNow.min;
	DeviceInfo[74] = TimeNow.hour&0x7f;
	DeviceInfo[73] = TimeNow.wday;
	DeviceInfo[72] = TimeNow.mday;
	DeviceInfo[71] = TimeNow.month;
	DeviceInfo[70] = (u8)TimeNow.year;
	
	for (i=0; i<DEVICE_INFO_LEN; i++)
	{
		Send_Buf[i] = DeviceInfo[i];
	}
	
	// 存储时不接收串口数据
	Img_process_finsh = 1;
	
	// 写入数据
	SD_Card_write_data(Send_Buf, FRAME_SIZE);

	// 图像处理标志，在没有处理完成之前，串口只接收数据，不处理数据
	Img_process_finsh = 0;

  return ;
}


/*
********************************************************************************
* Function Name  : SD_Card_storage_10_flush
* Description    : 图片数量到达10张进行缓存冲洗
* Input          : None
* Output         : None
* Return         : None
********************************************************************************
*/
void SD_Card_storage_10_flush(void)
{
		// 图像缓存计数
		static u8 Frame_cnt = 0;
	
		// 每10张图片冲洗一次缓存
		Frame_cnt++;
		if (Frame_cnt > 9)
		{
			Frame_cnt = 0;
			
			SD_Card_flush();
		}
}	


/*
********************************************************************************
* Function Name  : SD_Card_Open_Capsule_File
* Description    :打开以“胶囊序列号“命名的文件(如果不存在则创建)，打开前先判断tf
*                 剩余卡容量，剩余容量过小，则清空tf卡再打开或创建文件
* Input          : 胶囊序列号
* Output         : None
* Return         : None
********************************************************************************
*/
void SD_Card_Open_Capsule_File(u8 * tempoint)
{
  u8 i;
	char Capsule_fname[30]={NULL};

	//f_close(&Sd_f0); //关闭"0:default.JDF"文件
	
	//转换文件名
	sprintf(Capsule_fname,"0:Y%02x%02x%02x%02x%02x%02x%02x.YS",tempoint[1],
	        tempoint[2],tempoint[3],tempoint[4],tempoint[5],tempoint[6],tempoint[7]);
	
  for (i=0; i<3; i++)
  {			
		  Sd_fr = f_open(&Sd_f0, Capsule_fname,FA_READ|FA_OPEN_ALWAYS|FA_WRITE );	//打开或创建文件
			Sd_fr = f_lseek(&Sd_f0, Sd_f0.fsize);
												
      if(Sd_fr == FR_OK)
      {
        return ;
      }		
      delay_ms(500);  // 延时500ms
   }
   if(Sd_fr!=FR_OK)
   {
      NVIC_SystemReset();
   }
    
   return ;
}


/*
********************************************************************************
* Function Name  : SD_Card_Close_Capsule_File
* Description    :关闭以“胶囊序列号“命名的文件
* Input          : None
* Output         : None
* Return         : None
********************************************************************************
*/
void SD_Card_Close_Capsule_File(void)
{
  Sd_fr = f_close(&Sd_f0);  
	Sd_f0.fsize = 0; //文件大小清零
   return ;
}

/*
********************************************************************************
* Function Name  : Get_Card_Free_Capacity
* Description    :获取tf卡剩余容量
* Input          : None
* Output         : None
* Return         : None
********************************************************************************
*/
DWORD Get_Card_Free_Capacity(void)
{
	FATFS *pfs;
	DWORD fre_clust, fre_sect, tot_sect;
	char tembuff[30] = {0};
	
  //
  f_getfree("0:/", &fre_clust, &pfs);
	//
	tot_sect = (pfs->n_fatent - 2) * pfs->csize;
	fre_sect = fre_clust * pfs->csize;
	sprintf(tembuff,"TFC:%10lu KB\r\n", tot_sect*512/1024);
	USART3_Send(98, sizeof(tembuff), (u8*)tembuff);
	sprintf(tembuff,"TFFC:%10lu B\r\n", fre_sect*512);
  USART3_Send(98, sizeof(tembuff), (u8*)tembuff);
   return fre_sect*512;
}

/*
********************************************************************************
* Function Name  :scan_dele_files
* Description    :遍历删除文件夹下所有文件
* Input          : None
* Output         : None
* Return         : None
********************************************************************************
*/
void scan_dele_files(void)
{
	  FILINFO fileinfo;
    char *fn;   /* This function is assuming non-Unicode cfg. */ 
	  char lname[_MAX_LFN * 2 + 1] = {0};
	
		// 喂狗
		Iwdt_FeedDog();
		
	  //关闭"0:default.JDF"文件
	  f_close(&Sd_f0);
	
#if _USE_LFN_MAX_LFN * 2 + 1
    fileinfo.lfsize = _MAX_LFN * 2 + 1;
	  fileinfo.lfname = lname;
#endif
        while(f_readdir(&Sd_dir, &fileinfo)==FR_OK)//读取文件信息到文件状态结构体
        {
            if (!fileinfo.fname[0]) break;  //读取文件结束，跳出遍历循环
 
#if _USE_LFN
            fn = *fileinfo.lfname ? fileinfo.lfname : fileinfo.fname;
#else
             fn = FilInfo.fname;                  
#endif                                               
             f_unlink(fn); // 删除文件                                      
        }
    return ;
}

/*
********************************************************************************
* Function Name  : Alarm_Card_Free_Capacity
* Description    : tf卡剩余容量足1.5GB报警
* Input          : None
* Output         : None
* Return         : None
********************************************************************************
*/
void Alarm_Card_Free_Capacity(void)
{
	FATFS *pfs;
	DWORD fre_clust, fre_sect;
	
	//蜂鸣器开始响标志
	u32 tf_alarm_beep_on_time_flag = 0;
	
	//蜂鸣器持续响时间标志
	u32 tf_alarm_beep_time_flag = 0;
	
  //获取tf卡空闲簇数目
  f_getfree("0:/", &fre_clust, &pfs);
	//计算空闲扇区数目
	fre_sect = fre_clust * pfs->csize;
	//tf卡剩余容量小于1500000KB
	//if(fre_sect*512<1500000000ul)//tf卡剩余容量小于1500000KB
	if(fre_sect<fre_sect_num)//tf卡剩余容量小于fre_sect_num
	{
		while(1)
		{
			// 进入tf卡容量不足报警
		if((get_sys_tick_time()-tf_alarm_beep_on_time_flag)>tf_alarm_beep_on_cycly_ms)
		{
		 tf_alarm_beep_on_time_flag = get_sys_tick_time();
		 // 进入tf卡容量不足报警开
		 Beep_On();
		 tf_alarm_beep_time_flag = get_sys_tick_time();	
		}
		
		// 进入tf卡容量不足报警关
		if((get_sys_tick_time()- tf_alarm_beep_time_flag)>tf_alarm_beep_on_delay_ms)
		{
     Beep_Off();
		 tf_alarm_beep_time_flag = 0;
		}
		}
	}
   return ;
}

/*
********************************************************************************
* Function Name  : SD_Card_read_data
* Description    : 读SD卡数据函数
* Input          : array 读出数据指针    
*                  wr_len 读出数据长度
*                  adrr 数据起始地址
* Output         : None
* Return         : TRUE or FALSE
********************************************************************************
*/
 static BOOL SD_Card_read_rtc_data(u8 *array, u16 wr_len, u8 adrr)
{

   Sd_fr = f_lseek(&Sd_f0, adrr);	//定位到读取数据地址 
	 Sd_fr = f_read(&Sd_f0, array, wr_len, &Byte_n); //读数据
	 Sd_fr = f_lseek(&Sd_f0, 0);	//返回文件开始处
	  return TRUE;
}

/*
********************************************************************************
* Function Name  : SD_Card_read_data
* Description    : 从 SD 卡中读取RTC更新时间到设备信息存储数组
* Input          : None
* Output         : None
* Return         : None
********************************************************************************
*/
void Read_Rtc_From_SD_Card(void)
{
	// 没有图像数据不用读取
  if (Sd_f0.fsize > 20064)		
  {
  SD_Card_read_rtc_data(&DeviceInfo[83], 7, RTC_Uptime_Adr);
	}
}

/*
********************************************************************************
* Function Name  : Detect_Bind_Capsule_File
* Description    : 侦测绑定胶囊文件
* Input          : None
* Output         : None
* Return         : TRUE or FALSE
********************************************************************************
*/
BOOL Detect_Bind_Capsule_File(void)
{
	// 没有图像数据不用读取
  if (Sd_f0.fsize > 0)		
  {
  return TRUE;
	}
	else
	{
		return FALSE;
	}
}
