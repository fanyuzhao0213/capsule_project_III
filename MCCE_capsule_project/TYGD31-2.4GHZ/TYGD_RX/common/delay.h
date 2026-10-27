/*
****************************************************************************
* Copyright(C): TY Technology
* FileName    : delay.h
* Description : 延时
****************************************************************************
*/

#ifndef _DELAY_H_
#define _DELAY_H_



/*
********************************************************************************
* Function Name  : delay_us
* Description    : 延时 n 微秒，延时太长不精确，延时时长最好在秒以下
* Parameter      : - n: 微秒数
* Return         : None
********************************************************************************
*/
static inline void delay_us(VUINT32 n)
{
    while (n--)
    {
        __nop(); __nop(); __nop(); __nop(); __nop();
        __nop(); __nop(); __nop(); __nop(); __nop();
        __nop(); __nop(); __nop(); __nop(); __nop();
        __nop(); __nop(); __nop(); __nop(); __nop();
        __nop(); __nop(); __nop(); __nop(); __nop();
        __nop(); __nop(); __nop(); __nop(); __nop();
        __nop(); __nop(); __nop(); __nop(); __nop();
        __nop(); __nop(); __nop(); __nop(); __nop();
        __nop(); __nop(); __nop(); __nop(); __nop();
        __nop(); __nop(); __nop(); __nop(); __nop();
        __nop(); __nop(); __nop(); __nop(); __nop();
        __nop();
    }
}



extern void delay(UINT32 n);
extern void delay_ms(UINT32 n);



#endif /* _DELAY_H_ */

