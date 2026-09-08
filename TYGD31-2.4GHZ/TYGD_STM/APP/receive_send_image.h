/*
****************************************************************************
* Copyright(C): TY Technical
* FileName    : receive_send_image.h
* Author      : TY Technical Software Development Team
* Description : 从RF接收图像数据,发送到PC端/存储到SD卡
****************************************************************************
*/
#ifndef _RECEIVE_SEND_IMAGE_H_
#define _RECEIVE_SEND_IMAGE_H_

#include "common.h"

// 设备信息
#define DEVICE_INFO_LEN        (128U)

extern u8* Rec_Buf;
extern u8* Send_Buf;
extern u8 Img_data_reday;
extern u8 DeviceInfo[];
extern u32 Send_data_len;
extern u8 Img_process_finsh;

extern BOOL receive_image_data_end(void);
extern void send_image_to_PC(u8 cmd);

#endif 
