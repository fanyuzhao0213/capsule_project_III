/*
****************************************************************************
* Copyright(C): TY Technology
* FileName    : dev_rf1662.h
* Description : 配置rf1662
****************************************************************************
*/

#ifndef _RF1662_H_
#define _RF1662_H_

#include "common.h"



// RF1662寄存器地址
#define REGISTER_Addr               0x0000
#define RFFE_STATUS_Addr            0x001A
#define GROUP_SID_Addr              0x001B
#define PM_TRIG_Addr                0x001C
#define PRODUCT_ID_Addr             0x001D
#define MANUFACTURER_ID_Addr        0x001E
#define MAN_USID_Addr               0x001F
#define REVISION_ID_Addr            0x0021

// RF1662寄存器地址默认数据
#define REGISTER_Data               0x00
#define RFFE_STATUS_Data            0x00
#define GROUP_SID_Data              0x00
#define PM_TRIG_ACTIVE_Data         0x00  // 配置成ACTIVE模式  
#define PM_TRIG_STARTUP_Data        0x40  // 配置成STARTUP模式 
#define PM_TRIG_LOWPOWER_Data       0x80  // 配置成LOWPOWER模式 
#define PRODUCT_ID_Data             0x1A
#define MANUFACTURER_ID_Data        0x34
#define MAN_USID_Data               0x0A // 当有多个设备时可以通过配置低4位来给设备定义ID,最多15个 
#define REVISION_ID_Data            0x00

// ANT选择数据 RFMD tests with D7 = 0
#define ANT_TRX_NO                  0x00
#define ANT_TRX1                    0x02
#define ANT_TRX2                    0x0A
#define ANT_TRX3                    0x0E
#define ANT_TRX4                    0x0B
#define ANT_TRX5                    0x01
#define ANT_TRX6                    0x09
#define ANT_TRX7                    0x06
#define ANT_TRX8                    0x04
#define ANT_TRX9                    0x0C
#define ANT_TRX10                   0x08
#define ANT_TRX11                   0x03
#define ANT_TRX12                   0x05

// 开关触发配置
#define PM_TRIG_NO                  0x38       // 不需要0,1,2触发位触发,直接配置开关
#define PM_TRIG_00                  0x01       // 0触发器 (多个设备会使用到)
#define PM_TRIG_01                  0x02       // 1触发器
#define PM_TRIG_02                  0x04       // 2触发器  



extern void RF1662_Select_ANT(UINT8 ANT_TRX);
extern void RF1662_Init(void);



#endif  /* _DEV_RF1662_H_ */


