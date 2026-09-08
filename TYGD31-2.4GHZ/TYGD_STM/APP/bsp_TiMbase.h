/*
****************************************************************************
* Copyright(C): TY Technical
* FileName    : time_handle.c
* Author      : TY Technical Software Development Team
* Description : 通用定时器中断
****************************************************************************
*/
#include "stm32f10x.h"

#ifndef TIME_TEST_H
#define TIME_TEST_H





/********************通用定时器 TIMx,x[2,3,4,5]参数定义************/

#define             macTIMx                                TIM2
#define             macTIM_APBxClock_FUN                   RCC_APB1PeriphClockCmd
#define             macTIM_CLK                             RCC_APB1Periph_TIM2
#define             macTIM_IRQ                             TIM2_IRQn
#define             macTIM_INT_FUN                         TIM2_IRQHandler

//#define             macTIMx                                TIM3
//#define             macTIM_APBxClock_FUN                   RCC_APB1PeriphClockCmd
//#define             macTIM_CLK                             RCC_APB1Periph_TIM3
//#define             macTIM_IRQ                             TIM3_IRQn
//#define             macTIM_INT_FUN                         TIM3_IRQHandler

//中断周期为 = (1/(72MHZ /(psc+1)) * (arr+1))s
#define arr 999  //中断周期为 = 100ms
#define psc 7199 //中断周期为 = 100ms
/**************************函数声明********************************/
void TIMx_Configuration(void);


#endif	/* TIME_TEST_H */


