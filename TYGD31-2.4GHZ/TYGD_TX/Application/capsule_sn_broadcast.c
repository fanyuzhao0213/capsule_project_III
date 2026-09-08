/*
****************************************************************************
* Copyright(C): TY Technical
* FileName    : capsule_sn_broadcast.c
* Author      : TY Technical Software Development Team
* Description : 胶囊序列号广播功能
****************************************************************************
*/

#include "capsule_sn_broadcast.h"
#include "drv_radio.h"
#include "drv_timer.h"
#include "drv_device_id.h"
#include "drv_flash.h"
#include "define.h"

//最终输出的胶囊序列号
UINT8* capsule_sn_out ;

/*
********************************************************************************
* Function Name : capsule_sn_send
* Description   : 发送胶囊序列号
* Paramter      : None
* Return        : None
********************************************************************************
*/
static void capsule_sn_send(void)
{
    UINT8  buff[16] = {0};
    UINT8  i, len = 0;
    //UINT8* device_id = (UINT8 *)DEVICE_ID_ADDR;
		//UINT8* device_id = (UINT8*)FLASH_CAPSULE_SN_ADDR; //2019.12.16 改为设定的胶囊序列号

    // 图像开始包标识
    buff[len++]  = CMD_CAPSULE_SN_BROADCAST;

		// 胶囊序列号
		for (i = 0; i < DEVICE_ID_LEN; i++)
		{
				//buff[len++] = device_id[i];
			buff[len++] = capsule_sn_out[i];
		}

    // 发送
		radio_send(buff,len);
		
}


/*
********************************************************************************
* Function Name : capsule_id_send
* Description   : 发送胶囊id号
* Paramter      : None
* Return        : None
********************************************************************************
*/
static void capsule_id_send(void)
{
    UINT8  buff[16] = {0};
    UINT8  i, len = 0;
    UINT8* device_id = (UINT8 *)DEVICE_ID_ADDR;

    // 胶囊ID号广播命令
    buff[len++]  = CMD_CAPSULE_ID_BROADCAST;

		// 胶囊ID号
		for (i = 0; i < DEVICE_ID_LEN; i++)
		{
				buff[len++] = device_id[i];
		}

    // 发送三次
		radio_send(buff,len);
		radio_send(buff,len);
		radio_send(buff,len);
		
}


/*
********************************************************************************
* Function Name : capsule_sn_broadcast_process
* Description   : 胶囊序列号广播处理函数
* Paramter      : None
* Return        : None
********************************************************************************
*/
void capsule_sn_broadcast_process(void)
{
    static UINT32 last_time = 0;

    if ((last_time == 0) || ((timer2_get_time() - last_time) >= 500UL))
    {
        // 更新定时器计数
        last_time = timer2_get_time();

        // 无线通信使能
        radio_txen();
        
        // 发送胶囊序列号
        capsule_sn_send();

        // 无线通信关闭
        radio_disable();
    }
}


/*
********************************************************************************
* Function Name : capsule_id_broadcast_process
* Description   : 胶囊id号广播处理函数
* Paramter      : None
* Return        : None
********************************************************************************
*/
void capsule_id_broadcast_process(void)
{

        // 无线通信使能
        radio_txen();
        
        // 发送胶囊序列号
        capsule_id_send();

        // 无线通信关闭
        radio_disable();
}


/*
********************************************************************************
* Function Name : capsule_sn_set_reply
* Description   : 胶囊序列号设定应答，把设定后的胶囊序列号发给RF板
* Paramter      : None
* Return        : None
********************************************************************************
*/
static void capsule_sn_set_reply(void)
{
	UINT8  buff[16] = {0};
	UINT8  i, len = 0;
	//UINT8* device_id = (UINT8 *)DEVICE_ID_ADDR;
	UINT8* device_id = (UINT8*)FLASH_CAPSULE_SN_ADDR; //2019.12.16 改为设定的胶囊序列号

	// 图像开始包标识
	buff[len++]  = CMD_RSP_CAPSULE_SN_SET;

	// 胶囊序列号
	for (i = 0; i < DEVICE_ID_LEN; i++)
	{
			buff[len++] = device_id[i];
	}

	// 发送
	radio_send(buff,len);
	radio_send(buff,len);
	radio_send(buff,len);
	
}


/*
********************************************************************************
* Function Name : capsule_sn_set_checking
* Description   : 胶囊序列号设定检测
* Parameter     : None
* Return        : None
********************************************************************************
*/
void capsule_sn_set_checking(void)
{
		// 配置使用频段
		NRF_RADIO->FREQUENCY = RADIO_2490_FREQ ;//2490MHZ

		//读取芯片ID
		UINT8* device_buff = (UINT8 *)DEVICE_ID_ADDR;

		//3s内接收到设置命令标志
		BOOL in_3s_rec_set_cmd_flage = FALSE;

		UINT8* radio_data = radio_receive_one_packet1(30,CMD_REQ_CAPSULE_SN_SET,device_buff);//等待3s接收胶囊序列号设定命令
		//radio_data = radio_receive_one_packet1();

		// 接收到胶囊序列号设定命令，对比芯片ID，设定胶囊序列号，
		if (radio_data[0] == CMD_REQ_CAPSULE_SN_SET 
				&& radio_data[1] == device_buff[0] 
				&& radio_data[2] == device_buff[1]
				&& radio_data[3] == device_buff[2]
				&& radio_data[4] == device_buff[3]
				&& radio_data[5] == device_buff[4]
				&& radio_data[6] == device_buff[5]
				&& radio_data[7] == device_buff[6]
				&& radio_data[8] == device_buff[7]) 
		{
			
			//3s内接收到设置命令标志置1
			in_3s_rec_set_cmd_flage = TRUE;
			
			// 无线通信关闭
			radio_disable();

			// 擦除之前绑定的序列号
			flash_page_erase(FLASH_CAPSULE_SN_ADDR);
			
			// 写入新序列号
			flash_buff_write(FLASH_CAPSULE_SN_ADDR, &radio_data[9], 8);
			
			// 无线通信使能
			radio_txen();
			
			//延时30ms，等待RF板设定为接收模式
			//delay_ms(30);
			
			//胶囊序列号设定应答
			capsule_sn_set_reply();
			
			// 无线通信关闭
			radio_disable();
			
		}
		
		//等待重设序列号命令或者序列号设定确认命令
		if(in_3s_rec_set_cmd_flage)//3s内收到确认命令
		{
				radio_data = radio_receive_one_packet1(30,CMD_REQ_CAPSULE_SN_SET_OK,device_buff);//等待3s接收胶囊序列号设定确认命令
				
				// 接收到胶囊序列号设定命令，对比芯片ID，设定胶囊序列号，
						if (radio_data[0] == CMD_REQ_CAPSULE_SN_SET 
								&& radio_data[1] == device_buff[0] 
								&& radio_data[2] == device_buff[1]
								&& radio_data[3] == device_buff[2]
								&& radio_data[4] == device_buff[3]
								&& radio_data[5] == device_buff[4]
								&& radio_data[6] == device_buff[5]
								&& radio_data[7] == device_buff[6]
								&& radio_data[8] == device_buff[7])
						{
							// 无线通信关闭
							radio_disable();

							// 擦除之前绑定的序列号
							flash_page_erase(FLASH_CAPSULE_SN_ADDR);
							
							// 写入新序列号
							flash_buff_write(FLASH_CAPSULE_SN_ADDR, &radio_data[9], 8);
							
							// 无线通信使能
							radio_txen();
							
							//延时30ms，等待RF板设定为接收模式
							delay_ms(30);
							
							//胶囊序列号设定应答
							capsule_sn_set_reply();
							
							// 无线通信关闭
							radio_disable();
						}

	 }

		
		 UINT8 *capsule_sn = (UINT8*)FLASH_CAPSULE_SN_ADDR; 
			
			if( 0xFF == capsule_sn[0]
					&& 0xFF == capsule_sn[1]
					&& 0xFF == capsule_sn[2]
					&& 0xFF == capsule_sn[3]
					&& 0xFF == capsule_sn[4]
					&& 0xFF == capsule_sn[5]
					&& 0xFF == capsule_sn[6]
					&& 0xFF == capsule_sn[7])
			{
				capsule_sn_out = (UINT8 *)DEVICE_ID_ADDR;
			}
			else
			{
				capsule_sn_out = (UINT8 *)FLASH_CAPSULE_SN_ADDR;
			}
			 

	
	
	// 配置使用频段
	NRF_RADIO->FREQUENCY = RADIO_PUBLIC_FREQ;	
}

