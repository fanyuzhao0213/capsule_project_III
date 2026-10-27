/*
****************************************************************************
* Copyright(C): TY Technology
* FileName    : image.h
* Description : 图像数据处理
****************************************************************************
*/

#ifndef _IMAGE_H_
#define _IMAGE_H_

#include "common.h"



extern void image_begin_pkt_parse(UINT8* radio_data);
extern void image_data_pkt_parse(UINT8* radio_data);
extern void image_end_pkt_parse(UINT8* radio_data);



#endif /* _IMAGE_H_ */

