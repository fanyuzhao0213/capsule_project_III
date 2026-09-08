/*
****************************************************************************
* Copyright(C): TY Technical
* FileName    : jpeg_head.c
* Author      : TY Technical Software Development Team
* Description : jpeg格式头开始数据
****************************************************************************
*/
#ifndef _JPEG_HEAD_H_
#define _JPEG_HEAD_H_

#include "sys.h"

// 图片格式宏定义
#define IMAGE_HEADER_LEN       (686U)

#define FRAME_HEADER_SIZE      (128U)        
#define FRAME_DATA_SIZE        (20000UL)   

// 图片格式、存储卡芯片存储文件头格式大小
#define FRAME_SIZE             (20064UL)

#define FILE_HEADER_SIZE       (FRAME_SIZE * 10)

#define FILE_HEADER_BUFF_SIZE  (FILE_HEADER_SIZE / 10)


// 装载图片缓存的数组大小
#define BUFF_SIZE              (FRAME_DATA_SIZE + FRAME_HEADER_SIZE * 2 + 8)



extern u8 Data_Buf0[];
extern void jpeg_data_buf_Init(void);
#endif

