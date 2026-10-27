#ifndef __BSP_UART_H
#define __BSP_UART_H

#include "stm8s.h"

/* 蓝牙AT应答缓存容量。超过容量的应答会标记溢出，不作为有效响应。 */
#define PACKET_SIZE 32

/* 定义接收缓存结构体 */
typedef struct {
    uint8_t buffer[PACKET_SIZE];  // 存放数据的数组
    uint8_t len;                  // 当前已接收的长度
} UART_Packet_t;


/* 全局变量：接收包结构体 */
extern UART_Packet_t rx_packet;
/* 全局变量：接收完成的标志位 (1 = 收到完整的一包，0 = 还没收到/正在收) */
extern volatile uint8_t rx_packet_ready_flag; 
extern volatile uint8_t rx_packet_overflow_flag;
extern volatile uint8_t rx_packet_error_flag;

/*
 * ============================================================================
 * UART1 初始化
 *
 * UART1：
 *
 *     PD5 -> TX
 *     PD6 -> RX
 *
 * 波特率：
 *
 *     9600
 *
 * 格式：
 *
 *     8N1
 *
 * ============================================================================
 */
void BSP_UART_Init(void);


/*
 * ============================================================================
 * UART 发送一个字节
 * ============================================================================
 */
void BSP_UART_SendByte(uint8_t data);

/* 原样发送一组二进制字节 */
void BSP_UART_SendBuffer(const uint8_t *data, uint8_t len);


/*
 * ============================================================================
 * UART 发送字符串
 * ============================================================================
 */
void BSP_UART_SendString(const char *str);


/*
 * ============================================================================
 * UART 发送十六进制数据
 *
 * 例如：
 *
 *     BSP_UART_SendHex8(0x5A);
 *
 * 串口：
 *
 *     5A
 * ============================================================================
 */
void BSP_UART_SendHex8(uint8_t data);


#endif
