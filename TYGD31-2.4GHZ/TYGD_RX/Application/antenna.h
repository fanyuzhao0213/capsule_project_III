/*
****************************************************************************
* Copyright(C): TY Technology
* FileName    : antenna.h
* Description : 天线处理
****************************************************************************
*/

#ifndef _ANTENNA_H_
#define _ANTENNA_H_

#include "common.h"



// (实际)天线数量
#define ANTENNA_AMT_USED        (12)

// 天线数量上限
#define ANTENNA_AMT_MAX         (16)



extern UINT8 antenna_detect_cnt;

extern void antenna_read_info(UINT8 *buff);
extern void antenna_change_to_scan(void);
extern void antenna_change(UINT8 ant_num);
extern void antenna_init(void);
extern void antenna_process(void);



#endif /* _ANTENNA_H_ */

