#include "bsp_uart.h"
#include "stm8s_uart1.h"

UART_Packet_t rx_packet;
volatile uint8_t rx_packet_ready_flag = 0;
volatile uint8_t rx_packet_overflow_flag = 0;
volatile uint8_t rx_packet_error_flag = 0;
/*
 * ============================================================================
 * UART1 初始化
 * ============================================================================
 */
void BSP_UART_Init(void)
{
    /*
     * UART1 恢复默认状态。
     */
    UART1_DeInit();


    /*
     * UART1 参数：
     *
     *     BaudRate = 9600
     *
     *     WordLength = 8bit
     *
     *     StopBits = 1
     *
     *     Parity = None
     *
     *     SyncMode = Disable
     *
     *     Mode = TX + RX
     */
    UART1_Init(
        (uint32_t)9600,
        UART1_WORDLENGTH_8D,
        UART1_STOPBITS_1,
        UART1_PARITY_NO,
        UART1_SYNCMODE_CLOCK_DISABLE,
        UART1_MODE_TXRX_ENABLE
    );


    /*
     * 打开 UART1 接收中断。
     *
     * 接收到一个字节后：
     *
     *     RXNE = 1
     *
     * 然后进入 UART1 ISR。
     */
    UART1_ITConfig(
        UART1_IT_RXNE_OR,
        ENABLE
    );

    UART1_ITConfig(
        UART1_IT_IDLE,
        ENABLE
    );
    /*
     * 开启 UART1。
     */
    UART1_Cmd(ENABLE);
}


/*
 * ============================================================================
 * UART1 发送一个字节
 * ============================================================================
 */
void BSP_UART_SendByte(uint8_t data)
{
    /*
     * 写入 UART 数据寄存器。
     */
    UART1_SendData8(data);


    /*
     * 等待发送数据寄存器为空。
     */
    while (
        UART1_GetFlagStatus(UART1_FLAG_TXE) == RESET
    )
    {
        /* 等待 */
    }
}

void BSP_UART_SendBuffer(const uint8_t *data, uint8_t len)
{
    uint8_t i;

    for (i = 0; i < len; i++)
    {
        BSP_UART_SendByte(data[i]);
    }
}


/*
 * ============================================================================
 * UART1 发送字符串
 * ============================================================================
 */
void BSP_UART_SendString(const char *str)
{
    while (*str)
    {
        BSP_UART_SendByte(
            (uint8_t)*str++
        );
    }
}


/*
 * ============================================================================
 * UART1 发送一个 8bit 十六进制数
 *
 * 例如：
 *
 *     0xA5
 *
 * 输出：
 *
 *     A5
 * ============================================================================
 */
void BSP_UART_SendHex8(uint8_t data)
{
    const char hex[] = "0123456789ABCDEF";


    BSP_UART_SendByte(
        (uint8_t)hex[(data >> 4) & 0x0F]
    );


    BSP_UART_SendByte(
        (uint8_t)hex[data & 0x0F]
    );
}
