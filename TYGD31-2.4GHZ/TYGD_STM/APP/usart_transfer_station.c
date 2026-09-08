/*
****************************************************************************
* Copyright(C): TY Technical
* FileName    : usart_transfer_station.c
* Author      : TY Technical Software Development Team
* Description : 串口数据中转站
****************************************************************************
*/
#include "usart_transfer_station.h"
#include "drv_usart.h"
#include "receive_send_image.h"
#include "storage_card.h"
#include "do_main.h"

#define CAPSULE_ZISE    20


// 胶囊应答接收数组
u8 Capsule_Ack[CAPSULE_ZISE] = {0};

u8 Capsule_ReqAckOKFlag = 0;
u8 Capsule_DataLen = 0;

// 胶囊数据处理完成标志
u8 Capsule_process_flag = 0;


/*
********************************************************************************
* Function Name : capsule_sn_CMDDeal
* Description   : 胶囊序列号命令处理函数
* Paramter      : None
* Return        : None
********************************************************************************
*/
void capsule_sn_CMDDeal(void)  
{
    if (!Capsule_ReqAckOKFlag)
    {
        return ;
    }
			
		// 查询序列号，解绑，绑定时不接收串口2数据
		Img_process_finsh = 1;	
		
		// 重置串口2接收状态
		rx_step2 = 0;
		
    switch(Capsule_process_flag)
    {
			// 胶囊请求 请求数据流: PC->主控板->RF板 
			case CASE_REQ_CAPSULE_SN:	
			//胶囊文件标志清零，关闭已打开的胶囊文件
            if(!Capsule_File_FLAG)
						{
						 SD_Card_Close_Capsule_File();
						}							
            USART2_Send(Capsule_ReqAckOKFlag, Capsule_DataLen, Usart1_rec_buf); 
						Capsule_DataLen = 0;
						Capsule_process_flag = 0;
            break; 
			// 胶囊应答、广播  应答数据流: RF板->主控板->PC 
			case CASE_RSP_CAPSULE_SN:
            USART3_Send(Capsule_ReqAckOKFlag, Capsule_DataLen, Capsule_Ack);
						Capsule_DataLen = 0;           
						Capsule_process_flag = 0;
            break;
        default:
            break;
    }
		Capsule_ReqAckOKFlag = 0;
		
		Img_process_finsh = 0;
		
		return ;
}



