/*
****************************************************************************
* Copyright(C): TY Technology
* FileName    : define.h
* Author      : TY Technical Software Development Team
* Description : 全局宏定义
****************************************************************************
*/

#ifndef _DEFINE_H_
#define _DEFINE_H_



#ifndef NULL
    #define NULL    (0)    // 空指针
#endif


// 应用软件地址
#define FLASH_APP_ADDR               (0)


/**************************************APP_FLASH_ADDR*************************************/
// 绑定的胶囊序列号存储地址
#define FLASH_CAPSULE_SN_ADDR            (1024 * 256)

/**************************************end APP_FLASH_ADDR**********************************/


// 设备信息大小
#define DEVICE_INFO_SIZE                 (128)

// 图像帧数据大小
#define FRAME_DATA_SIZE                  (20000)


#endif /* _DEFINE_H_ */

