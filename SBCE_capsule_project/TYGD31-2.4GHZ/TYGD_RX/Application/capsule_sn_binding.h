/*
****************************************************************************
* Copyright(C): TY Technology
* FileName    : capsule_sn_binding.h
* Description : 胶囊序列号绑定处理
****************************************************************************
*/

#ifndef _CAPSULE_SN_BINDING_H_
#define _CAPSULE_SN_BINDING_H_

#include "common.h"



extern BOOL capsule_sn_is_bound(void);
extern void capsule_sn_query(void);
extern void capsule_sn_unbind(void);
extern void capsule_sn_bind(UINT8* sn_addr);
extern void capsule_sn_set(UINT8* sn_addr);
extern void capsule_sn_set_confirm(UINT8* sn_addr);
extern void capsule_sn_forward(UINT8* radio_data);
extern void capsule_id_forward(UINT8* radio_data);
extern void capsule_sn_set_reply(UINT8* radio_data);
extern void radio_channel_frequency_set(UINT8 channel_freq_val);



#endif /* _CAPSULE_SN_BINDING_H_ */

