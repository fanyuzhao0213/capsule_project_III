/*
****************************************************************************
* Copyright(C): TY Technology
* FileName    : drv_radio.h
* Author      : TY Technical Software Development Team
* Description : 配置无线通信功能
****************************************************************************
*/

#ifndef _DRV_RADIO_H_
#define _DRV_RADIO_H_

#include "common.h"



#define PACKET_SIZE                  (254)              // 数据包大小
#define PACKET_IMG_DATA_SIZE         (PACKET_SIZE - 12) // 数据包中图像数据的大小

#define RADIO_PUBLIC_FREQ            (0)                // RF公共频段2.400GHZ
#define RADIO_2490_FREQ              (90)               // RF2490MHZ频段



extern void radio_txen(void);
extern void radio_rxen(void);
extern void radio_disable(void);
extern void radio_reset(void);
extern void radio_init(void);
extern void radio_send(UINT8* buff, UINT8 len);

extern UINT8* radio_receive_one_packet(void);
extern UINT8* radio_receive_one_packet1(UINT16 delay_time,UINT8 cmd,UINT8* device_id_buff);


extern void radio_change_to_private_freq(void);
extern UINT8* radio_get_rcvd_packet(void);

#endif /* _DRV_RADIO_H_ */

