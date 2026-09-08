/*
****************************************************************************
* Copyright(C): TY Technical
* FileName    : jpeg_head.h
* Author      : TY Technical Software Development Team
* Description : JPEG 格式头与图像缓存相关宏定义
****************************************************************************
*/
#ifndef _JPEG_HEAD_H_
#define _JPEG_HEAD_H_

#include "sys.h"

// ┌─────────────────────────────────────────────────────────┐
// │  JPEG 帧格式相关常量                                      │
// └─────────────────────────────────────────────────────────┘

// JPEG 标准头部长度（FF D8 + APP0 + DQT + DHT + SOF + SOS 等标记）
// = 686 字节（固定，由 CX93510 JPEG 编码器决定）
#define IMAGE_HEADER_LEN       (686U)

// 每帧头部长度（含数据头 / 设备信息 / 预头部）
// = 128 字节
#define FRAME_HEADER_SIZE      (128U)

// 单帧图像最大数据长度（不含 header）
// = 20000 字节（CX93510 最大输出 JPEG 大小）
#define FRAME_DATA_SIZE        (20000UL)

// ┌─────────────────────────────────────────────────────────┐
// │  SD 卡存储帧结构                                         │
// └─────────────────────────────────────────────────────────┘

// 单帧总字节数 = FRAME_HEADER_SIZE + IMAGE_HEADER_LEN + FRAME_DATA_SIZE
// = 128 + 686 + 20000 = 20814？
// 实际值 20064：JPEG header 与数据有重叠（约 750 字节）
// 这是 CX93510 JPEG 输出的实际长度（已实测）
#define FRAME_SIZE             (20064UL)

// 单个 .YS 文件的总头部长度（10 份 FRAME_SIZE）
// = 200640 字节（FATFS 写入时的"虚拟头"）
#define FILE_HEADER_SIZE       (FRAME_SIZE * 10)

// 文件头缓冲大小（单次 f_write 的字节数）
// = FILE_HEADER_SIZE / 10 = 20064 字节
#define FILE_HEADER_BUFF_SIZE  (FILE_HEADER_SIZE / 10)


// ┌─────────────────────────────────────────────────────────┐
// │  接收/发送缓冲区大小                                       │
// └─────────────────────────────────────────────────────────┘

// 装载图片缓存的数组大小（Data_Buf0）
// = FRAME_DATA_SIZE + FRAME_HEADER_SIZE × 2 + 8
// = 20000 + 128×2 + 8 = 20264 字节
// 多出来的 8 字节是 UART3 接收时的额外预留
#define BUFF_SIZE              (FRAME_DATA_SIZE + FRAME_HEADER_SIZE * 2 + 8)



// ┌─────────────────────────────────────────────────────────┐
// │  全局声明                                                  │
// └─────────────────────────────────────────────────────────┘

// JPEG 数据缓存（实际大小 BUFF_SIZE = 20264）
// 布局：[预头部 128B][JPEG header 686B][JPEG 数据 变长]
extern u8 Data_Buf0[];

// JPEG 数据缓存初始化（填充模板预头部 + JPEG header）
extern void jpeg_data_buf_Init(void);

#endif

