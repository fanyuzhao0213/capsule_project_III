/*
****************************************************************************
* Copyright(C): TY Technical
* FileName    : drv_power_mode.h
* Author      : TY Technical Software Development Team
* Description : 配置主控板电源模式
****************************************************************************
*/
#ifndef _DRV_POWER_MODE_H_
#define _DRV_POWER_MODE_H_

#include "common.h"


/*
    上电模式
    SDOE_USB--1：USB_MCU模式 
							0：USB_AU 模式         
		           
*/
#define USB_MCU         1
#define USB_AU 	        0		
#define CH_REC_SIGNAL_STRENGTH_LOW_TH 75 // 通道RSSI弱阈值75
#define CH_REC_SIGNAL_STRENGTH_ALA_TH 0 // 通道RSSI弱报警阈值


extern void Power_Mode_GPIOConfig(void);
extern void power_up(void);
extern void power_down(void);
extern void Beep_Init(void);
extern void Beep_Toggle(BOOL state);
extern void Beep_On(void);
extern void Beep_Off(void);
extern u8 Rssi_Max_Data(u8 *buff_info);

#endif

