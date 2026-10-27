/*
****************************************************************************
* Copyright(C): TY Technology
* FileName    : typedef.h
* Description : 全局数据类型定义
****************************************************************************
*/

#ifndef _TYPEDEF_H_
#define _TYPEDEF_H_



// 普通数据类型定义
typedef signed char                 INT8;
typedef signed short int            INT16;
typedef signed int                  INT32;
typedef unsigned char               UINT8;
typedef unsigned short int          UINT16;
typedef unsigned int                UINT32;
typedef unsigned char               u8;
typedef unsigned short int          u16;
typedef unsigned int                u32;
typedef float                       FLOAT;
typedef double                      DOUBLE;
typedef char                        CHAR;

// volatile 数据类型定义
typedef volatile signed char        VINT8;
typedef volatile signed short int   VINT16;
typedef volatile signed int         VINT32;
typedef volatile unsigned char      VUINT8;
typedef volatile unsigned short int VUINT16;
typedef volatile unsigned int       VUINT32;
typedef volatile float              VFLOAT;
typedef volatile double             VDOUBLE;
typedef volatile char               VCHAR;

// 定义布尔类型
typedef enum
{
    FALSE = 0,
    TRUE = 1
} BOOL;

// 开关状态定义
typedef enum
{
    OFF = 0,
    ON = 1
} STATUS;

// 引脚电平状态定义
typedef enum
{
    PIN_LOW = 0,
    PIN_HIGH = 1
} PIN_STATUS;

// 函数指针类型定义
typedef void (*FUNCTION_t)(void);



#endif /* _TYPEDEF_H_ */

