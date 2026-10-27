/*
****************************************************************************
* Copyright(C): TY Technical
* FileName    : dev_sensor.h
* Author      : TY Technical Software Development Team
* Description : 感光芯片OV7670配置数据
****************************************************************************
*/

#ifndef _DEV_SENSORS_H_
#define _DEV_SENSORS_H_


#include "common.h"

extern void sensor_init(void);
extern void sensor_sleep(void);
extern void sensor_wakeup(void);


#endif /* _DEV_SENSORS_H_ */

