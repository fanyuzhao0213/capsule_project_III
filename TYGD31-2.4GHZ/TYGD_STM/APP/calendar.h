/*
****************************************************************************
* Copyright(C): TY Technical
* FileName    : calendar.h
* Author      : TY Technical Software Development Team
* Description : 日历
****************************************************************************
*/
#ifndef _CALENDAR_H_
#define _CALENDAR_H_

#include "common.h"

typedef     struct
{       /* date and time components */
    u8     sec;    //senconds after the minute, 0 to 59
    u8     min;    //minutes after the hour, 0 to 59
    u8     hour;   //hours since midnight, 0 to 23
		u8     wday;   //days since Sunday, 0 to 6
    u8     mday;   //day of the month, 1 to 31
    u8     month;  //months of the year, 1 to 12
    u16    year;   //years, START_YEAR to START_YEAR+135  
    u16    yday;   //days of the year, 1 to 255
}Calendar_Def;


extern void GetTime(Calendar_Def *pTimeNow);
extern void SetTime0(u32 tt);
extern u32 RTC2058_GetCounter(void);

extern void BcdSetRtcTime(u8 *tempoint);
extern void GetBcdTime(Calendar_Def *pTimeNow);

extern Calendar_Def TimeNow;

#endif

