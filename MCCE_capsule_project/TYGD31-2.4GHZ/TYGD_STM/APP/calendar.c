/*
****************************************************************************
* Copyright(C): TY Technical
* FileName    : calendar.c
* Author      : TY Technical Software Development Team
* Description : 日历
****************************************************************************
*/
#include "calendar.h"
#include "drv_sd2058.h"

// 定义当前时间
Calendar_Def TimeNow;

const u8 mDayNoLeap[13] =
{
  0, 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31
};

const u8 mDayLeap[13] =
{
  0, 31, 29, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31
};


#define   START_YEAR      (u16)(2000)//default 2000, Jan.1, 00:00:00
#define   SEC_IN_DAY      (u32)(24*60*60)//one day includes 86400 seconds
#define   DAY_IN_YEAR(nYear) (IsLeap(nYear) ? 366 : 365)

/***********************************************************************
* Function Name  : IsLeap
* Description    : Check whether the past year is leap or not.
* Input          : 4 digits year number
* Return         : 1: leap year. 0: not leap year
***********************************************************************/
static  u8  IsLeap(s16 nYear)
{
		if(nYear % 4 != 0)      return 0;
    if(nYear % 100 != 0)    return 1;
    return (u8)(nYear % 400 == 0);
}


/*
********************************************************************************
* Function Name  : Updata_SD2085_Time
* Description    : 获取SD2058时钟时间
* Input          : None
* Output         : None
* Return         : None
********************************************************************************
*/
void Updata_SD2085_Time(Calendar_Def* pTimeNow)
{
  u8 i, *pTmp;
	
	pTimeNow->year -= START_YEAR;
	pTmp = (u8*)pTimeNow;
	for(i = 0; i < 7; i++)
	{
		pTmp[i] = ((pTmp[i] / 10) << 4) + (pTmp[i] % 10);
	}
	pTimeNow->hour |= 0x80;
	
	Enable_Write_Protect();
	SD2058_Write_Bytes(0x00, (u8*)pTimeNow, 7);
	Disable_Write_Protect();
	
	RTC2058_GetCounter();
}


/*******************************************************************************
* Function Name  : MyMakeTime
* Description    : Form a 32 bit second counting value from calendar.
* Input          : pointer to a calendar struct
* Return         : 32 bit second counting value
*******************************************************************************/
u32 MyMakeTime(Calendar_Def *pCalendar)
{
  u32 TotalSeconds = pCalendar->sec;
  u16 nYear = pCalendar->year;
  u16 nMonth = pCalendar->month;
  
  if((nYear < START_YEAR) || (nYear > (START_YEAR + 135))) 
    return 0;//out of year range
  
  TotalSeconds += (u32)pCalendar->min * 60;//contribution of minutes
  TotalSeconds += (u32)pCalendar->hour * 3600;//contribution of hours
  //contribution of mdays
  TotalSeconds += (u32)(pCalendar->mday - 1) * SEC_IN_DAY;
  
  if(IsLeap(nYear))
    while(nMonth > 1)
      TotalSeconds += (u32)mDayLeap[--nMonth] * SEC_IN_DAY;
  else
    while(nMonth > 1)//contribution of months
      TotalSeconds += (u32)mDayNoLeap[--nMonth] * SEC_IN_DAY;
  
  while(nYear > START_YEAR)//contribution of years
    TotalSeconds += (u32)DAY_IN_YEAR(--nYear) * SEC_IN_DAY;
  
  return TotalSeconds;
} 


/*******************************************************************************
* Function Name  : MyLocalTime
* Description    : Form a calendar from 32 bit second counting.
* Input          : pointer to a 32 bit second value, option pointer to a struct
* Return         : Calendar structure
*******************************************************************************/
Calendar_Def MyLocalTime(u32 TotalSecs, Calendar_Def *pCalendar)
{
   Calendar_Def Calendar;//Local variables 
   u32 TotalDays, Remainder;
      
   Calendar.year = START_YEAR;//Calendar initialization
   Calendar.month = 1;  
   Calendar.mday = 1;
   Calendar.yday = 1;  
   
   TotalDays = TotalSecs/SEC_IN_DAY;//Split days from seconds
   Remainder = TotalSecs%SEC_IN_DAY;
   
   Calendar.hour = Remainder/3600;//Get clock in day
   Calendar.min = (Remainder/60)%60;
   Calendar.sec = Remainder%60;
   
   while(TotalDays >= DAY_IN_YEAR(Calendar.year))
      TotalDays -= DAY_IN_YEAR(Calendar.year++);
   Calendar.yday += TotalDays;//Get years and days in year    
   
   if(IsLeap(Calendar.year))//Get months and days in month
      while(TotalDays >= mDayLeap[Calendar.month])
        TotalDays -= mDayLeap[Calendar.month++];
   else
      while(TotalDays >= mDayNoLeap[Calendar.month])
        TotalDays -= mDayNoLeap[Calendar.month++];
   Calendar.mday += TotalDays;
   
   if(pCalendar)//Copy Calendar if necessary
      *pCalendar = Calendar;
   
   return Calendar;
}


/*
********************************************************************************
* Function Name  : RTC2058_GetCounter
* Description    : read the total secends from the RTC IC
* Input          : None
* Output         : None
* Return         : 总秒数
********************************************************************************
*/
u32 RTC2058_GetCounter(void)
{
	u32 totalSec;
	Calendar_Def curTime;
	
	//时分秒读取并换算BCD码
	SD2058_ReadBytes_From_Begin((u8*)&curTime, 7);
	curTime.sec = (curTime.sec >> 4) * 10 + (curTime.sec & 0xf);
	curTime.min = (curTime.min >> 4) * 10 + (curTime.min & 0xf);
	curTime.hour &= 0x7f;	//最高位为24/12时间置标识
	curTime.hour = (curTime.hour >> 4) * 10 + (curTime.hour & 0xf);
	//年月日BCD换算
	curTime.mday = (curTime.mday >> 4) * 10 + (curTime.mday & 0xf);
	curTime.month = (curTime.month >> 4) * 10 + (curTime.month & 0xf);
	curTime.year &= 0x00ff;	
	curTime.year = ((u8)curTime.year >> 4) * 10 + ((u8)curTime.year & 0xf);	
	curTime.year += START_YEAR;
	
	totalSec = MyMakeTime(&curTime);
	
	return totalSec;
}


/*
********************************************************************************
* Function Name  : RTC2058_SetCounter
* Description    : updata RTC IC time with total seconds
* Input          : None
* Output         : None
* Return         : None
********************************************************************************
*/
void RTC2058_SetCounter(u32 updataTime)
{
	Calendar_Def curTime;
	curTime = MyLocalTime(updataTime, 0);
	Updata_SD2085_Time(&curTime);
}


/*
********************************************************************************
* Function Name  : GetTime
* Description    : Get time to struct *pCalendar from RTC.
* Input          : None
* Output         : None
* Return         : None
********************************************************************************
*/
void GetTime(Calendar_Def *pTimeNow)
{ 
	u32 t_t;
  
  // call function in stm32f10x lib
  t_t = RTC2058_GetCounter(); 
  
  // call function in local file
  *pTimeNow = MyLocalTime(t_t, 0); 
}

/*
********************************************************************************
* Function Name  : GetBcdTime
* Description    : Get bcd format time to struct *pCalendar from RTC.
* Input          : None
* Output         : None
* Return         : None
********************************************************************************
*/
void GetBcdTime(Calendar_Def *pTimeNow)
{  
	SD2058_ReadBytes_From_Begin((u8*)pTimeNow, 7);	
}


/*
********************************************************************************
* Function Name  : SetTime0
* Description    : 设置时间
* Input          : None
* Output         : None
* Return         : None
********************************************************************************
*/
void SetTime0(u32 tt)
{	
	//call function in stm32f10x lib	
  RTC2058_SetCounter(tt);
}

/*
********************************************************************************
* Function Name  : BcdSetRtcTime
* Description    : 根据BCD吗设置RTC时间
* Input          : None
* Output         :  
* Return         : None
********************************************************************************
*/
void BcdSetRtcTime(u8 *tempoint)
{	
	  u8 tembuff[7];
	  tembuff[0] = tempoint[6];
		tembuff[1] = tempoint[5];
		tembuff[2] = tempoint[4];
		tembuff[3] = tempoint[3];
		tembuff[4] = tempoint[2];
		tembuff[5] = tempoint[1];
		tembuff[6] = tempoint[0];
		Enable_Write_Protect();		
		tembuff[2] |= 0x80;			// hour and 24hours system
		SD2058_Write_Bytes(0x00, tembuff, 7);
		Disable_Write_Protect(); 
}


