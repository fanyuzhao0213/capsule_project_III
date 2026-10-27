/*
****************************************************************************
* Copyright(C): TY Technical
* FileName    : drv_sd2058.h
* Author      : TY Technical Software Development Team
* Description : sd2058时钟芯片底层驱动
****************************************************************************
*/
#ifndef _DRV_SD2058_H_
#define _DRV_SD2058_H_

#include "sys.h"


extern void SD2058_Init(void);
extern ErrorStatus SD2058_ReadBytes(u8 readAddr, u8* buffer, u8 readNum);
extern ErrorStatus SD2058_ReadBytes_From_Begin(u8* buffer, u8 readNum);
extern ErrorStatus SD2058_Write_OneByte(u8 writeAddr, u8 data);
extern ErrorStatus SD2058_Write_Bytes(u8 writeAddr, u8* buffer, u8 writeNum);
extern void Enable_Write_Protect(void);
extern void Disable_Write_Protect(void);

extern u8 sd2058_begin_state;
#endif

