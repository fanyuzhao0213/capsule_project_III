/*
****************************************************************************
* Copyright(C): TY Technical
* FileName    : drv_usart.h
* Author      : TY Technical Software Development Team
* Description : 串口驱动
****************************************************************************
*/
#ifndef _DRV_USART_H_
#define _DRV_USART_H_

#include "common.h"

extern u8 Usart1_rec_buf[300];

extern vu8  rx_step2;

extern void USART3_Configuration(void);
extern void USART2_Configuration(void);

extern void USART3_Send(u8 cmd, u16 data_len, u8 *u1_tx_buff);

extern void USART2_Receive(u8 rx_buff);
extern void USART2_Send(u8 cmd, u16 data_len, u8 *u2_tx_buff);

#endif


