/*
****************************************************************************
* Copyright(C): TY Technical
* FileName    : time_handle.h
* Author      : TY Technical Software Development Team
* Description : 时间处理
****************************************************************************
*/
#ifndef _TIME_HANDLE_H_
#define _TIME_HANDLE_H_

#include "common.h"

extern u32 For_time_now;

extern u8 Rx_time_flinsh;

extern void switch_on_uptime(void);
extern void MCU_uptime_now(void);
extern void PC_cmd_uptime(void);
extern void switch_on_uptime_init(void);

extern void PC_cmd_bcd_uptime(void);
extern void RTC_uptime_now(void);

#endif

