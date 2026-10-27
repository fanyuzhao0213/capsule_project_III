/*
****************************************************************************
* Copyright(C): TY Technology
* FileName    : domain.c
* Description : 主循环处理
****************************************************************************
*/

#include "domain.h"
#include "drv_wdt.h"
#include "drv_radio.h"
#include "drv_uart.h"
#include "drv_systick.h"
#include "image.h"
#include "capsule_sn_binding.h"



/*
********************************************************************************
* Function Name : radio_receive_process
* Description   : 无线通信接收处理函数
* Parameter     : None
* Return        : None
********************************************************************************
*/
static void radio_receive_process(void)
{
    UINT8* radio_data = radio_get_rcvd_packet();

    // 判断是否接收到射频数据
    if(radio_data != NULL)
    {
        UINT8 cmd = radio_data[0];
        
        switch (cmd)
        {
            case CMD_IMG_BEGIN_PACKET:
                image_begin_pkt_parse(radio_data);
                break;        
            case CMD_IMG_DATA_PACKET:
                image_data_pkt_parse(radio_data);
                break;
            case CMD_IMG_END_PACKET:
                image_end_pkt_parse(radio_data);
                break;
            case CMD_CAPSULE_SN_BROADCAST:
                capsule_sn_forward(radio_data);
                break;
						case CMD_RSP_CAPSULE_SN_SET:
                capsule_sn_set_reply(radio_data);
                break;
						case CMD_CAPSULE_ID_BROADCAST:
                capsule_id_forward(radio_data);
                break;
            default:
                break;
        }
    }
		
    return;
}


/*
********************************************************************************
* Function Name : uart_reveive_process
* Description   : 串口通信接收处理函数
* Parameter     : None
* Return        : None
********************************************************************************
*/
static void uart_reveive_process(void)
{
    UINT8 buff[32] = {0};
    
    if (TRUE == uart_reveive(buff))
    {
        UINT8  cmd = buff[0];

        switch (cmd)
        {
            case CMD_REQ_CAPSULE_SN_QUERY:
                capsule_sn_query();
                break;
            case CMD_REQ_CAPSULE_SN_UNBIND:
                capsule_sn_unbind();
                break;
            case CMD_REQ_CAPSULE_SN_BIND:
                capsule_sn_bind(buff + 3);
                break;    
            case CMD_REQ_CAPSULE_SN_SET:
                capsule_sn_set(buff + 3);
                break;   
            case CMD_REQ_CAPSULE_SN_SET_OK :
                capsule_sn_set_confirm(buff + 3);
                break; 
            case CMD_REQ_RADIO_CHANNEL_FREQ_SET:
						    radio_channel_frequency_set(buff[3]);
                break; 												
            default:
                break;
        }
    } 
}


/*
********************************************************************************
* Function Name : domain
* Description   : 主循环
* Parameter     : None
* Return        : None
********************************************************************************
*/
void domain(void)
{
    static UINT32 process_time = 0;
		UINT32 current_time = 0;
    
    while(1)
    {
        current_time = systick_get_time();

        // 每50ms执行一次
        if (current_time - process_time >= 50)
        {
            process_time = current_time;
            
            wdt_feed();
            uart_reveive_process();
        }
        radio_receive_process();
    } 
}


