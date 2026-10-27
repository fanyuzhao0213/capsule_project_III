/*
****************************************************************************
* Copyright(C): TY Technology
* FileName    : dev_cx93510.h
* Author      : TY Technical Software Development Team
* Description : 配置cx93510
****************************************************************************
*/


#ifndef _DEV_CX93510_H_
#define _DEV_CX93510_H_

#include "common.h"



#define CX93510_IMG_DATA_MIN         (0x2B8) // 图像数据最小字节数


extern void cx93510_init_pin(void);

extern BOOL cx93510_start(void);
extern void cx93510_close(void);
extern void cx93510_init(void);
extern void cx93510_setup(void);
extern UINT16 cx93510_get_image(UINT8 *image_buf);

extern BOOL FRAME_DATA_SIZE_GT_20KB_FLAGE ;//帧图像数据大于20KB标志

#endif /* _DEV_CX93510_H_ */

