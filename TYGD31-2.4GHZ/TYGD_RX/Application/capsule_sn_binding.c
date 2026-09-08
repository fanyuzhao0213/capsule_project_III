/*
****************************************************************************
* Copyright(C): TY Technology
* FileName    : capsule_sn_binding.c
* Description : 胶囊序列号绑定处理
****************************************************************************
*/

#include "capsule_sn_binding.h"
#include "drv_uart.h"
#include "drv_flash.h"
#include "drv_radio.h"
#include "drv_flash.h"
#include "drv_device_id.h"
#include <string.h>

#define RF_RELLY 0



/*
********************************************************************************
* Function Name : capsule_sn_is_bound
* Description   : 判断胶囊序列号是否绑定
* Parameter     : None
* Return        : TRUE表示已绑定，FALSE表示未绑定
********************************************************************************
*/
BOOL capsule_sn_is_bound(void)
{
    UINT8 i;
    BOOL  is_sn_bound = FALSE;
    UINT8 *capsule_sn = (UINT8*)FLASH_CAPSULE_SN_ADDR;

    // 判断胶囊序列号是否已绑定
    for (i = 0; i < DEVICE_ID_LEN; i++)
    {
        if (capsule_sn[i] != 0xFF)
        {
            is_sn_bound = TRUE;
            break;
        }
    }

    return is_sn_bound;
}


/*
********************************************************************************
* Function Name : capsule_sn_query
* Description   : 胶囊序列号查询
* Parameter     : None
* Return        : None
********************************************************************************
*/
void capsule_sn_query(void)
{
    UINT8 i, len = 0, sum_check = 0;
    UINT8 buff[16];
    UINT8 *capsule_sn = (UINT8*)FLASH_CAPSULE_SN_ADDR;

    // 头标识
    buff[len++] = 0xFF;
    buff[len++] = 0x55;
    buff[len++] = 0x12;
    buff[len++] = 0x34;

    // CMD
    buff[len++] = CMD_RSP_CAPSULE_SN_QUERY;

    // 数据长度
    buff[len++] = 0;
    buff[len++] = DEVICE_ID_LEN;

    // 数据
    for (i = 0; i < DEVICE_ID_LEN; i++, len++)
    {
        buff[len] = capsule_sn[i];
        sum_check += buff[len];
    }

    // 校验和
    buff[len++] = sum_check;

    // 发送应答
    uart_send(buff, len);
}


/*
********************************************************************************
* Function Name : capsule_sn_unbind
* Description   : 胶囊序列号解绑
* Parameter     : None
* Return        : None
********************************************************************************
*/
void capsule_sn_unbind(void)
{
    UINT8 len = 0, sum_check = 0;
    UINT8 buff[16];

    // 无线通信关闭
    radio_disable();

    // 擦除已绑定序列号
    flash_page_erase(FLASH_CAPSULE_SN_ADDR);
    
    // 无线通信接收使能
    radio_rxen();

    // 头标识
    buff[len++] = 0xFF;
    buff[len++] = 0x55;
    buff[len++] = 0x12;
    buff[len++] = 0x34;

    // CMD
    buff[len++] = CMD_RSP_CAPSULE_SN_UNBIND;

    // 数据长度
    buff[len++] = 0;
    buff[len++] = 1;

    // 数据
    buff[len++] = TRUE;
    sum_check   = TRUE;

    // 校验和
    buff[len++] = sum_check;

    // 发送应答
    uart_send(buff, len);
    delay_ms(50);
    uart_send(buff, len);
    delay_ms(50);
    uart_send(buff, len);
}


/*
********************************************************************************
* Function Name : capsule_sn_bind
* Description   : 胶囊序列号绑定
* Parameter     : - sn_addr: 需要绑定的序列号地址
* Return        : None
********************************************************************************
*/
void capsule_sn_bind(UINT8* sn_addr)
{
    UINT8 len = 0, sum_check = 0;
    UINT8 buff[16];

    // 无线通信关闭
    radio_disable();

    // 擦除之前绑定的序列号
    flash_page_erase(FLASH_CAPSULE_SN_ADDR);
    
    // 写入新序列号
    flash_buff_write(FLASH_CAPSULE_SN_ADDR, sn_addr, 8);

    // 头标识
    buff[len++] = 0xFF;
    buff[len++] = 0x55;
    buff[len++] = 0x12;
    buff[len++] = 0x34;
    
    // CMD
    buff[len++] = CMD_RSP_CAPSULE_SN_BIND;

    // 数据长度
    buff[len++] = 0;
    buff[len++] = 1;

    // 数据
    buff[len++] = TRUE;
    sum_check   = TRUE;

    // 校验和
    buff[len++] = sum_check;

    // 发送应答
    uart_send(buff, len);

    // 无线通信接收使能
    radio_rxen();
}

/*
********************************************************************************
* Function Name : capsule_sn_set
* Description   : 胶囊序列号设定
* Parameter     : sn_addr: 需要设定的序列号
* Return        : None
********************************************************************************
*/
void capsule_sn_set(UINT8* sn_addr)
{
	  // 配置使用频段
		NRF_RADIO->FREQUENCY = RADIO_2490_FREQ ;//2490MHZ
	
		//延时30ms，等待RF板切换频率
		delay_ms(30);
	
    UINT8 len = 0;
    UINT8 buff[16] = {0};
		
    // CMD 胶囊序列号设定请求
    buff[len++] = CMD_REQ_CAPSULE_SN_SET; //胶囊序列号设定请求
		
    // 胶囊ID号+序列号
    buff[len++] = sn_addr[0];
    buff[len++] = sn_addr[1];
    buff[len++] = sn_addr[2];
    buff[len++] = sn_addr[3];
    buff[len++] = sn_addr[4];
    buff[len++] = sn_addr[5];
    buff[len++] = sn_addr[6];
    buff[len++] = sn_addr[7];
		buff[len++] = sn_addr[8];
    buff[len++] = sn_addr[9];
    buff[len++] = sn_addr[10];
    buff[len++] = sn_addr[11];
    buff[len++] = sn_addr[12];
    buff[len++] = sn_addr[13];
    buff[len++] = sn_addr[14];
    buff[len++] = sn_addr[15];
		
		//切换到发射模式
    radio_disable();
    radio_txen(); 
		// 发送
    radio_send(buff, len);
		radio_send(buff, len);
		radio_send(buff, len);
		//切换到接收模式
		radio_disable();
    radio_rxen();
		
#ifdef RF_RELPY
    UINT8 sum_check = 0
	  len = 0;
   // 头标识
    buff[len++] = 0xFF;
    buff[len++] = 0x55;
    buff[len++] = 0x12;
    buff[len++] = 0x34;
    
    // CMD
    buff[len++] = CMD_RSP_CAPSULE_SN_SET;

    // 数据长度
    buff[len++] = 0;
    buff[len++] = 1;

    // 数据
    buff[len++] = TRUE;
    sum_check   = TRUE;

    // 校验和
    buff[len++] = sum_check;

    // 发送应答
    uart_send(buff, len);
#endif
}


/*
********************************************************************************
* Function Name : capsule_sn_set_confirm
* Description   : 胶囊序列号设定确认
* Parameter     : sn_addr: 确认命令
* Return        : None
********************************************************************************
*/
void capsule_sn_set_confirm(UINT8* sn_addr)
{
    UINT8 len = 0,sum_check = 0;
    UINT8 buff[16] = {0};
		
    // CMD 胶囊序列号设定确认
    buff[len++] = CMD_REQ_CAPSULE_SN_SET_OK; //胶囊序列号设定请求
		
    // 胶囊ID号
    buff[len++] = sn_addr[0];
    buff[len++] = sn_addr[1];
    buff[len++] = sn_addr[2];
    buff[len++] = sn_addr[3];
    buff[len++] = sn_addr[4];
    buff[len++] = sn_addr[5];
    buff[len++] = sn_addr[6];
    buff[len++] = sn_addr[7];
		
		//切换到发射模式
    radio_disable();
    radio_txen(); 
		// 发送
    radio_send(buff, len);
		radio_send(buff, len);
		radio_send(buff, len);
		
		//从无线通信频道地址中读出频道值
		UINT8 tep_radio_channel_freq_val = *((UINT8*)FLASH_RADIO_CHANNEL_FREQ_ADDR);
		if(tep_radio_channel_freq_val>100)
		{
			tep_radio_channel_freq_val = 100;
		}
		
		// 配置使用频段 
		NRF_RADIO->FREQUENCY = tep_radio_channel_freq_val;
		
		//切换到接收模式
		radio_disable();
    radio_rxen();
  
	  len = 0;
   // 头标识
    buff[len++] = 0xFF;
    buff[len++] = 0x55;
    buff[len++] = 0x12;
    buff[len++] = 0x34;
    
    // CMD
    buff[len++] = CMD_RSP_CAPSULE_SN_SET_OK;

    // 数据长度
    buff[len++] = 0;
    buff[len++] = 1;

    // 数据
    buff[len++] = TRUE;
    sum_check   = TRUE;

    // 校验和
    buff[len++] = sum_check;

    // 发送应答
    uart_send(buff, len);
		
}



/*
********************************************************************************
* Function Name : forward_capsule_sn
* Description   : 转发胶囊序列号
* Parameter     : - radio_data: 无线通信数据指针
* Return        : None
********************************************************************************
*/
void capsule_sn_forward(UINT8* radio_data)
{
    UINT8 i, len = 0, sum_check = 0;
    UINT8 buff[16];
    
    buff[len++] = 0xFF;
    buff[len++] = 0x55;
    buff[len++] = 0x12;
    buff[len++] = 0x34;
    buff[len++] = CMD_CAPSULE_SN_BROADCAST;
    buff[len++] = 0;
    buff[len++] = 8;

    for (i = 0; i < 8; i++, len++)
    {
        buff[len] = radio_data[1 + i];
        sum_check += buff[len];
    }

    buff[len++] = sum_check;

    uart_send(buff, len);
}


/*
********************************************************************************
* Function Name : forward_capsule_id
* Description   : 转发胶囊序列号
* Parameter     : - radio_data: 无线通信数据指针
* Return        : None
********************************************************************************
*/
void capsule_id_forward(UINT8* radio_data)
{
    UINT8 i, len = 0, sum_check = 0;
    UINT8 buff[16];
    
    buff[len++] = 0xFF;
    buff[len++] = 0x55;
    buff[len++] = 0x12;
    buff[len++] = 0x34;
    buff[len++] = CMD_CAPSULE_ID_BROADCAST;
    buff[len++] = 0;
    buff[len++] = 8;

    for (i = 0; i < 8; i++, len++)
    {
        buff[len] = radio_data[1 + i];
        sum_check += buff[len];
    }

    buff[len++] = sum_check;

    uart_send(buff, len);
}


/*
********************************************************************************
* Function Name : capsule_sn_set_reply
* Description   : 转发胶囊序列号设定应答
* Parameter     : - radio_data: 无线通信数据指针
* Return        : None
********************************************************************************
*/
void capsule_sn_set_reply(UINT8* radio_data)
{
    UINT8 i, len = 0, sum_check = 0;
    UINT8 buff[16];
    
    buff[len++] = 0xFF;
    buff[len++] = 0x55;
    buff[len++] = 0x12;
    buff[len++] = 0x34;
    buff[len++] = CMD_RSP_CAPSULE_SN_SET;
    buff[len++] = 0;
    buff[len++] = 8;

    for (i = 0; i < 8; i++, len++)
    {
        buff[len] = radio_data[1 + i];
        sum_check += buff[len];
    }

    buff[len++] = sum_check;

    uart_send(buff, len);
}


/*
********************************************************************************
* Function Name : radio_channel_frequency_set
* Description   : 无线通信频道设置
* Parameter     : - channel_freq_val: 需要设定的通信频道值
* Return        : None
********************************************************************************
*/
void radio_channel_frequency_set(UINT8 channel_freq_val)
{
    UINT8 len = 0, sum_check = 0;
    UINT8 buff[16];
	
		// 无线通信关闭
		radio_disable();
	    // 擦除之前无线通信频道
    flash_page_erase(FLASH_RADIO_CHANNEL_FREQ_ADDR);
    
    // 写入新无线通信频道
    flash_buff_write(FLASH_RADIO_CHANNEL_FREQ_ADDR, &channel_freq_val, 1);
	
	  if(channel_freq_val>100)
		{
			channel_freq_val= 100;
		}

	  // 配置使用频段
	  NRF_RADIO->FREQUENCY = channel_freq_val ;//Radio channel frequency:0-100
				
//		//切换到接收模式
//		radio_disable();
    radio_rxen();

    // 头标识
    buff[len++] = 0xFF;
    buff[len++] = 0x55;
    buff[len++] = 0x12;
    buff[len++] = 0x34;
    
    // CMD
    buff[len++] = CMD_RSP_RADIO_CHANNEL_FREQ_SET;

    // 数据长度
    buff[len++] = 0;
    buff[len++] = 1;

    // 数据
    buff[len++] = channel_freq_val;
    sum_check   = channel_freq_val;

    // 校验和
    buff[len++] = sum_check;

    // 发送应答
    uart_send(buff, len);

}


