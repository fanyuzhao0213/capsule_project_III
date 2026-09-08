/*
****************************************************************************
* Copyright(C): TY Technical
* FileName    : storage_card.h
* Author      : TY Technical Software Development Team
* Description : 存储卡存储处理
****************************************************************************
*/
#ifndef _STORAGE_CARD_H_
#define _STORAGE_CARD_H_

#include "common.h"
#include "ff.h"

extern u8 Frame_out_range;

extern void SD_Card_Open_File(void);
extern void SD_Card_write_file_header(void);
extern void SD_Card_storage_imgdata(void);
extern void SD_Card_storage_10_flush(void);
extern void SD_Card_Open_Capsule_File(u8 * tempoint);
extern DWORD Get_Card_Free_Capacity(void);
extern void scan_dele_files(void);
extern void Alarm_Card_Free_Capacity(void);
extern void Read_Rtc_From_SD_Card(void);
extern BOOL Detect_Bind_Capsule_File(void);
extern void SD_Card_Close_Capsule_File(void);
#endif 

