/*
****************************************************************************
* Copyright(C): TY Technology
* FileName    : drv_radio.c
* Author      : TY Technical Software Development Team
* Description : 配置无线通信功能
****************************************************************************
*/
 
#include "drv_radio.h"
#include "drv_device_id.h"
#include "nrf.h"
#include "drv_wdt.h"
#include "dev_led.h"
#include "drv_flash.h"
#include "define.h"
#include <string.h>



#define PACKET0_S1_SIZE                  (0UL)    // S1 size in bits
#define PACKET0_S0_SIZE                  (0UL)    // S0 size in bits
#define PACKET0_PAYLOAD_SIZE             (0UL)    // payload size in bits
#define PACKET1_BASE_ADDRESS_LENGTH      (4UL)    // base address length in bytes
#define PACKET1_STATIC_LENGTH            (254UL)  // static length in bytes
#define PACKET1_PAYLOAD_SIZE             (254UL)  // payload size in bytes

#define led_on_time             (1000UL)  // 设置胶囊序列号时led 点亮时间

// RF信号强度异常值，大于或等于该值，表示信号强度太弱，不符合要求
#define RADIO_RSSI_ERROR_VALUE           (0x40)

// 无线通信中断请求优先级
#define RADIO_IRQ_PRIORITY               (2)

// 无线通信队列buffer数量
#define RADIO_QUEUE_BUFF_SUM             (18)


// 判断无线通信数据队列是否已满
#define RADIO_QUEUE_IS_FULL()     (((radio_queue.tail + 2) % RADIO_QUEUE_BUFF_SUM == radio_queue.head) ? TRUE : FALSE)

// 判断无线通信数据队列是否为空
#define RADIO_QUEUE_IS_EMPTY()    ((radio_queue.tail == radio_queue.head) ? TRUE : FALSE)



// 无线通信数据队列结构体定义
typedef struct
{
    UINT8 head;
    UINT8 tail;
    UINT8 buff[RADIO_QUEUE_BUFF_SUM][PACKET_SIZE];
} RADIO_QUEUE_t;



// 无线通信数据包
static UINT8 radio_packet[PACKET_SIZE] = {0};

// 无线通信数据队列
static RADIO_QUEUE_t radio_queue = {0};


/*
********************************************************************************
* Function Name : radio_txen
* Description   : RF发送使能
* Parameter     : None
* Return        : None
********************************************************************************
*/
void radio_txen(void)
{
    // 启动发送模式
    NRF_RADIO->EVENTS_READY = 0U;
    NRF_RADIO->TASKS_TXEN   = 1U;
    while (NRF_RADIO->EVENTS_READY == 0U);
    {
        // Do nothing.
    }
    
    return;
}


/*
********************************************************************************
* Function Name : radio_rxen
* Description   : RF数据接收使能
* Parameter     : None
* Return        : None
********************************************************************************
*/
void radio_rxen(void)
{
    // Shortcuts
    NRF_RADIO->SHORTS = 0x00000021;

    // 启动接收模式
    NRF_RADIO->EVENTS_READY = 0;    
    NRF_RADIO->TASKS_RXEN   = 1;
    while (NRF_RADIO->EVENTS_READY == 0);
    {
        // Do nothing.
    }

    // 复位相关寄存器
    NRF_RADIO->EVENTS_RSSIEND = 0;
    NRF_RADIO->EVENTS_CRCOK   = 0;
    NRF_RADIO->EVENTS_END     = 0;
    
    // 设置中断标识位
    NRF_RADIO->INTENSET = RADIO_INTENSET_END_Msk;
    
    return;
}


/*
********************************************************************************
* Function Name : radio_init
* Description   : 无线通信功能初始化
* Parameter     : None
* Return        : None
********************************************************************************
*/
void radio_init(void)
{
    // Radio config
    NRF_RADIO->TXPOWER   = (RADIO_TXPOWER_TXPOWER_Pos4dBm << RADIO_TXPOWER_TXPOWER_Pos);
    NRF_RADIO->MODE      = (RADIO_MODE_MODE_Nrf_2Mbit << RADIO_MODE_MODE_Pos);

    // Radio address config
    NRF_RADIO->PREFIX0 = 0xC4C3C2E7UL;
    NRF_RADIO->PREFIX1 = 0xC5C6C7C8UL;

    NRF_RADIO->BASE0 = 0xE7E7E7E7UL;  // Base address for prefix 0 converted to nRF24L series format
    NRF_RADIO->BASE1 = 0x00C2C2C2UL;  // Base address for prefix 1-7 converted to nRF24L series format
  
    NRF_RADIO->TXADDRESS   = 0x00UL;  // Set device address 0 to use when transmitting
    NRF_RADIO->RXADDRESSES = 0x01UL;  // Enable device address 0 to use to select which addresses to receive

    // Packet configuration
    NRF_RADIO->PCNF0 = (PACKET0_S1_SIZE << RADIO_PCNF0_S1LEN_Pos) |
                       (PACKET0_S0_SIZE << RADIO_PCNF0_S0LEN_Pos) |
                       (PACKET0_PAYLOAD_SIZE << RADIO_PCNF0_LFLEN_Pos);

    // Packet configuration
    NRF_RADIO->PCNF1 = (RADIO_PCNF1_WHITEEN_Disabled << RADIO_PCNF1_WHITEEN_Pos)    |
                       (RADIO_PCNF1_ENDIAN_Big << RADIO_PCNF1_ENDIAN_Pos)           |
                       (PACKET1_BASE_ADDRESS_LENGTH << RADIO_PCNF1_BALEN_Pos)       |
                       (PACKET1_STATIC_LENGTH << RADIO_PCNF1_STATLEN_Pos)           |
                       (PACKET1_PAYLOAD_SIZE << RADIO_PCNF1_MAXLEN_Pos);
    
    // CRC Config
    NRF_RADIO->CRCCNF = (RADIO_CRCCNF_LEN_Two << RADIO_CRCCNF_LEN_Pos); // Number of checksum bits
    if ((NRF_RADIO->CRCCNF & RADIO_CRCCNF_LEN_Msk) == (RADIO_CRCCNF_LEN_Two << RADIO_CRCCNF_LEN_Pos))
    {
        NRF_RADIO->CRCINIT = 0xFFFFUL;   // Initial value      
        NRF_RADIO->CRCPOLY = 0x11021UL;  // CRC poly: x^16+x^12^x^5+1
    }
    else if ((NRF_RADIO->CRCCNF & RADIO_CRCCNF_LEN_Msk) == (RADIO_CRCCNF_LEN_One << RADIO_CRCCNF_LEN_Pos))
    {
        NRF_RADIO->CRCINIT = 0xFFUL;    // Initial value
        NRF_RADIO->CRCPOLY = 0x107UL;   // CRC poly: x^8+x^2^x^1+1
    }

    // Radio send/receive data buffer config
    NRF_RADIO->PACKETPTR = (UINT32)radio_packet;

    // 配置RADIO中断
    NVIC_ClearPendingIRQ(RADIO_IRQn);
    NVIC_SetPriority(RADIO_IRQn, RADIO_IRQ_PRIORITY);
    NVIC_EnableIRQ(RADIO_IRQn);
    
		// 配置使用频段
		NRF_RADIO->FREQUENCY = RADIO_PUBLIC_FREQ;
}


/*
********************************************************************************
* Function Name : radio_send
* Description   : 无线通信数据发送
* Parameter     : - buff: 待发送的数据buffer
*                 - len : 待发送的数据长度
* Return        : None
********************************************************************************
*/
void radio_send(UINT8* buff, UINT8 len)
{
    // 把数据复制到发送区
    memcpy(radio_packet, buff, len);

    // 启动数据传输
    NRF_RADIO->TASKS_START = 1U;
    NRF_RADIO->EVENTS_END  = 0U;
    while(NRF_RADIO->EVENTS_END == 0)
    {
        // Do nothing.
    }    

    // 清空无线通信数据发送buff
    memset((UINT8*)radio_packet, 0, sizeof(radio_packet));
}



/*
********************************************************************************
* Function Name : radio_disable
* Description   : 无线通信(收/发)关闭
* Parameter     : None
* Return        : None
********************************************************************************
*/
void radio_disable(void)
{
    // 关闭无线通信
    NRF_RADIO->EVENTS_DISABLED = 0;
    NRF_RADIO->TASKS_DISABLE   = 1;
    while(NRF_RADIO->EVENTS_DISABLED == 0U)
    {
        // Do nothing.
    }

    // 清除中断标识位
    NRF_RADIO->INTENCLR = RADIO_INTENSET_END_Msk;

    // Shortcuts
    NRF_RADIO->SHORTS = 0x00000000;
}


/*
********************************************************************************
* Function Name : radio_receive_one_packet
* Description   : 无线通信数据接收(仅接收一个数据包)
* Parameter     : None
* Return        : 接收成功则返回数据地址，否则返回NULL
********************************************************************************
*/
UINT8* radio_receive_one_packet(void)
{
    UINT16 count = 0;
    UINT8* ret   = radio_packet;

    // 启动接收模式
    NRF_RADIO->EVENTS_READY = 0;    
    NRF_RADIO->TASKS_RXEN   = 1;
    while(NRF_RADIO->EVENTS_READY == 0);
    {
        // Do nothing.
    }

    // 开始接收数据
    NRF_RADIO->EVENTS_CRCOK = 0;
    NRF_RADIO->EVENTS_END   = 0;
    NRF_RADIO->TASKS_START  = 1;
    while(NRF_RADIO->EVENTS_END == 0)
    {
        delay_us(10);
        count++;
        if(count >= 300)
        {
            ret = NULL;
            break;
        }
    }

    // 检测CRC校验
    if (NRF_RADIO->EVENTS_CRCOK == 0)
    {
        ret = NULL;  // CRC校验未通过
    }
    
    // 关闭无线通信
    NRF_RADIO->EVENTS_DISABLED = 0;
    NRF_RADIO->TASKS_DISABLE   = 1;
    while(NRF_RADIO->EVENTS_DISABLED == 0)
    {
        // Do nothing.
    }
    
    return ret;
}


/*
********************************************************************************
* Function Name : radio_receive_one_packet1
* Description   : 无线通信数据接收(仅接收一个数据包)
* Parameter     : 等待delay_time ms;cmd 命令; device_id_buff 指向芯片ID的指针
* Return        : 接收成功则返回数据地址，否则返回NULL
********************************************************************************
*/
UINT8* radio_receive_one_packet1(UINT16 delay_time,UINT8 cmd,UINT8* device_id_buff)
{
    UINT16 count = 0;
   // UINT8* ret   = radio_packet;
	  BOOL delay_falge = FALSE;

	  // 清空无线通信数据发送buff
    memset((UINT8*)radio_packet, 0, sizeof(radio_packet));
	
    // 启动接收模式
    NRF_RADIO->EVENTS_READY = 0;    
    NRF_RADIO->TASKS_RXEN   = 1;
    while(NRF_RADIO->EVENTS_READY == 0)
    {
        // Do nothing.
    }

		while(radio_packet[0]!=cmd
			    && radio_packet[1] != device_id_buff[0] 
					&& radio_packet[2] != device_id_buff[1]
					&& radio_packet[3] != device_id_buff[2]
					&& radio_packet[4] != device_id_buff[3]
					&& radio_packet[5] != device_id_buff[4]
					&& radio_packet[6] != device_id_buff[5]
					&& radio_packet[7] != device_id_buff[6]
					&& radio_packet[8] != device_id_buff[7])
		{
				// 开始接收数据
				NRF_RADIO->EVENTS_CRCOK = 0;
				NRF_RADIO->EVENTS_END   = 0;
				NRF_RADIO->TASKS_START  = 1;
				while(NRF_RADIO->EVENTS_END == 0)
				{
					  delay_ms(100);//10
						count++;			  
					  if(!(count%10))
						{
							led_set(ON);
							delay_us(led_on_time);
						  wdt_feed();
							led_set(OFF);
						}
						if(count >= delay_time)
						{  
		            //ret = NULL;
							delay_falge = TRUE;
		            break;
						}
					
				}
				
				if(delay_falge)
				{
					delay_falge = FALSE;
					break;
				}

				// 检测CRC校验
				if (NRF_RADIO->EVENTS_CRCOK == 0)
				{
						//ret = NULL;  // CRC校验未通过
					  //break;
				 // 清空无线通信数据发送buff
         memset((UINT8*)radio_packet, 0, sizeof(radio_packet));
				}
		}
			
    UINT8* ret   = radio_packet;
			
    // 关闭无线通信
    NRF_RADIO->EVENTS_DISABLED = 0;
    NRF_RADIO->TASKS_DISABLE   = 1;
    while(NRF_RADIO->EVENTS_DISABLED == 0)
    {
        // Do nothing.
    }
    
    return ret;
}


/*
********************************************************************************
* Function Name : radio_get_rcvd_packet
* Description   : 读取(RF)接收到的数据包
* Parameter     : None
* Return        : 返回数据的首地址
********************************************************************************
*/
UINT8* radio_get_rcvd_packet(void)
{
    UINT8* ret = NULL;
    
    if (RADIO_QUEUE_IS_EMPTY() == FALSE)
    {
        ret = radio_queue.buff[radio_queue.head];
        radio_queue.head = (radio_queue.head + 1) % RADIO_QUEUE_BUFF_SUM;
    }

    return ret;
}


/*
********************************************************************************
* Function Name : RADIO_IRQHandler
* Description   : 无线通信中断函数
* Parameter     : None
* Return        : None
********************************************************************************
*/
void RADIO_IRQHandler(void)
{
    if (NRF_RADIO->EVENTS_END && (NRF_RADIO->INTENSET & RADIO_INTENSET_END_Msk))
    {
        if (NRF_RADIO->EVENTS_CRCOK == 1)
        {
            NRF_RADIO->EVENTS_CRCOK = 0;

            if (RADIO_QUEUE_IS_FULL() == FALSE)
            {
                // 把无线通信数据包拷贝到数据队列
                memcpy(radio_queue.buff[radio_queue.tail], radio_packet, PACKET_SIZE);
                
                // 调整数据队列指针
                radio_queue.tail = (radio_queue.tail + 1) % RADIO_QUEUE_BUFF_SUM;
            }
        }

        // 启动下一次数据接收
        NRF_RADIO->EVENTS_END   = 0;
        NRF_RADIO->TASKS_START  = 1;
    }
}


