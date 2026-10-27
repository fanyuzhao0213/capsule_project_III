/*
****************************************************************************
* Copyright(C): TY Technical
* FileName    : capsule_sn_broadcast.h
* Author      : TY Technical Software Development Team
* Description : 胶囊序列号广播功能
****************************************************************************
*/

#ifndef _CAPSULE_SN_BROADCAST_H_
#define _CAPSULE_SN_BROADCAST_H_

#include "common.h"



extern void capsule_sn_broadcast_process(void);

extern void capsule_id_broadcast_process(void);

extern void capsule_sn_set_checking(void);

//最终输出的胶囊序列号
extern UINT8* capsule_sn_out ;

#endif /* _CAPSULE_SN_BROADCAST_H_ */

