/*
****************************************************************************
* Copyright(C): TY Technology
* FileName    : drv_radio.h
* Description : 配置无线通信功能
****************************************************************************
*/

#ifndef _DRV_RADIO_H_
#define _DRV_RADIO_H_

#include "common.h"
#include "nrf.h"



#define PACKET_SIZE                  (254)              // 数据包大小
#define PACKET_IMG_DATA_SIZE         (PACKET_SIZE - 12) // 数据包中图像数据的大小
#define RADIO_2490_FREQ              (90)               // RF2490MHZ频段,设定序列号使用该频道


// 无线信号强度异常值，大于或等于该值，表示信号强度太弱，不符合要求
#define RADIO_RSSI_ERROR_VALUE       (0x40)



// 无线通信模式枚举类型定义
typedef enum
{
    RADIO_COMM_MODE_RX = 0,         // 接收模式
    RADIO_COMM_MODE_TX,             // 发送模式
} RADIO_COMM_MODE_t;



extern UINT8 radio_rssi_value;

extern void radio_reset(void);
extern void radio_disable(void);
extern void radio_init(void);
extern void radio_rxen(void);
extern void radio_txen(void);
extern void radio_send(UINT8* buff, UINT8 len);
extern UINT8* radio_receive_one_packet(void);
extern UINT8* radio_get_rcvd_packet(void);
extern void radio_set_comm_mode(RADIO_COMM_MODE_t rf_mode);



#endif /* _DRV_RADIO_H_ */

