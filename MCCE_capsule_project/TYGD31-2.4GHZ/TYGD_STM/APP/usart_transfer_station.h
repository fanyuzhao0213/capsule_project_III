/*
****************************************************************************
* Copyright(C): TY Technical
* FileName    : usart_transfer_station.h
* Author      : TY Technical Software Development Team
* Description : 串口数据中转站
****************************************************************************
*/
#ifndef __CAPSULE_SN_H__
#define __CAPSULE_SN_H__

#include "sys.h"

extern u8 Capsule_Ack[];
extern u8 Capsule_ReqAckOKFlag;
extern u8 Capsule_process_flag;
extern u8 Capsule_DataLen;

extern void capsule_sn_CMDDeal(void);


#endif


