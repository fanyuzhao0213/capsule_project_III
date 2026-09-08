/*
****************************************************************************
* Copyright(C): TY Technology
* FileName    : image.c
* Author      : TY Technical Software Development Team
* Description : 图像数据处理
****************************************************************************
*/

#include "image.h"
#include "drv_radio.h"
#include "drv_device_id.h"
#include "dev_cx93510.h"
#include "dev_sensor.h"
#include "dev_led.h"
#include "drv_flash.h"
#include "define.h"
#include "capsule_sn_broadcast.h"
#include <string.h>


// 图像数据buffer
UINT8 image_buff[FRAME_DATA_SIZE] = {0};

// 图像数据包数量
UINT16 img_data_pkt_num = 0;

// 图像数据字节长度
UINT16 img_data_len = 0;

// 图像帧标识，用于防止乱码
UINT8 image_id = 0;

// 图像数据校验和
UINT8 image_data_check_sum = 0;


/*
********************************************************************************
* Function Name : image_read
* Description   : 读取图像
* Parameter     : None
* Return        : TRUE: 读取图像成功    FALSE: 读取图像失败
********************************************************************************
*/
static BOOL image_read(void)
{
    UINT32 count = 0;
    
    img_data_len = 0;
    
    if (cx93510_start() == TRUE)
    {
			cx93510_setup();
	
			while (1)
			{
					// 读取图像
					img_data_len = cx93510_get_image(image_buff);
					
					if (img_data_len > 0)  // 如果读取图像成功则退出
					{
							break;
					}
					else if (count >= 180) // 如果读取图像超时则退出
					{
							led_set(OFF);      // 关闭闪光灯
							sensor_sleep();    // 感光芯片进入休眠
							break;
					}
					else if(FRAME_DATA_SIZE_GT_20KB_FLAGE ) //帧图像数据大于20KB则退出
					{
						FRAME_DATA_SIZE_GT_20KB_FLAGE = FALSE; //帧图像数据大于20KB标志清零
						led_set(OFF);      // 关闭闪光灯
						sensor_sleep();    // 感光芯片进入休眠			
						break;
					}
					else
					{
							delay_ms(1);
							count++;
					}
			}
    }
    
    cx93510_close();

    return ((img_data_len > 0) ? TRUE : FALSE);
}


/*
********************************************************************************
* Function Name : image_begin_pkt_send
* Description   : 发送图像开始包
* Parameter     : None
* Return        : None
********************************************************************************
*/
static void image_begin_pkt_send(void)
{
    UINT8  buff[32] = {0};
    UINT8  i, pkt_len = 0;
    //UINT8* device_id = (UINT8*)DEVICE_ID_ADDR;   
    // CMD
    buff[pkt_len++] = CMD_IMG_BEGIN_PACKET;
    // 图像id
    buff[pkt_len++] = image_id;
    
		// 胶囊序列号
		for (i = 0; i < DEVICE_ID_LEN; i++)
		{
				//buff[pkt_len++] = device_id[i];
			buff[pkt_len++] = capsule_sn_out[i];
		}
			
    // 图像数据长度
    buff[pkt_len++] = (UINT8)(img_data_len >> 8);
    buff[pkt_len++] = (UINT8)(img_data_len >> 0);
    // 图像数据包数量
    buff[pkt_len++] = (UINT8)(img_data_pkt_num >> 8);
    buff[pkt_len++] = (UINT8)(img_data_pkt_num >> 0);
    // 胶囊软件版本号
    buff[pkt_len++] = VERSION_MAIN;
    buff[pkt_len++] = VERSION_SUB;
    buff[pkt_len++] = VERSION_TEST;

    // 发送
    radio_send(buff, pkt_len);
}


/*
********************************************************************************
* Function Name : image_data_pkt_send
* Description   : 发送图像数据包
* Parameter     : None
* Return        : None
********************************************************************************
*/
static void image_data_pkt_send(void)
{
    UINT8  buff[PACKET_SIZE] = {0};
    UINT16 i, j, data_len, pkt_len = 0;   
    UINT8  *data_addr = NULL;
    //UINT8  *device_id = (UINT8*)DEVICE_ID_ADDR;

    // CMD
    buff[pkt_len++] = CMD_IMG_DATA_PACKET;
    
    // 图像id
    buff[pkt_len++] = image_id;
    
   	// 胶囊序列号
		for (i = 0; i < DEVICE_ID_LEN; i++)
		{
				//buff[pkt_len++] = device_id[i];
			buff[pkt_len++] = capsule_sn_out[i];
		}
		
    // 图像数据和校验复位
    image_data_check_sum = 0;
		
    // 循环发送图像数据包
    for (i = 0; i < img_data_pkt_num; i++)
    {
        pkt_len = 10;
        
        // 图像包序号
        buff[pkt_len++] = i / 256;
        buff[pkt_len++] = i % 256;
        
        // 图像数据
        if (img_data_len - (i * PACKET_IMG_DATA_SIZE) >= PACKET_IMG_DATA_SIZE)
        {
            data_len = PACKET_IMG_DATA_SIZE;
        }
        else
        {
            data_len = img_data_len - (i * PACKET_IMG_DATA_SIZE);
        }
        data_addr = image_buff + (i * PACKET_IMG_DATA_SIZE);
        memcpy(buff + pkt_len, data_addr, data_len);
        pkt_len += data_len;

        //校验和
        for (j = 0; j < data_len; j++)
        {
            image_data_check_sum += data_addr[j];
        }
        
        // 发送
        radio_send(buff, pkt_len);
    }
}


/*
********************************************************************************
* Function Name : image_end_pkt_send
* Description   : 发送图像结束包
* Parameter     : None
* Return        : None
********************************************************************************
*/
static void image_end_pkt_send(void)
{
    UINT8  buff[16] = {0};
    UINT8  i, pkt_len = 0;
    //UINT8* device_id = (UINT8*)DEVICE_ID_ADDR;
    
    // CMD
    buff[pkt_len++] = CMD_IMG_END_PACKET;

    // 图像id
    buff[pkt_len++] = image_id;

		// 胶囊序列号
		for (i = 0; i < DEVICE_ID_LEN; i++)
		{
				//buff[pkt_len++] = device_id[i];
			buff[pkt_len++] = capsule_sn_out[i];
		}

    // 图像数据校验和
    buff[pkt_len++] = image_data_check_sum;

    // 发送
    radio_send(buff, pkt_len);
}


/*
********************************************************************************
* Function Name : image_send
* Description   : 发送图像
* Parameter     : None
* Return        : None
********************************************************************************
*/
static void image_send(void)
{
    // 图像数据和校验复位
    image_data_check_sum = 0;

    // 计算图像数据包数量
    img_data_pkt_num = ((img_data_len - 1) / PACKET_IMG_DATA_SIZE) + 1;
    
    // RF发送使能
    radio_txen();
    
    // 图像开始包
    image_begin_pkt_send();
    image_begin_pkt_send();

    // 图像数据包
    image_data_pkt_send();

    // 图像结束包
    image_end_pkt_send();

    // 无线通信关闭
    radio_disable();
}


/*
********************************************************************************
* Function Name : image_resend
* Description   : 图像重发
* Parameter     : None
* Return        : None
********************************************************************************
*/
static void image_resend(void)
{
    // RF发送使能
    radio_txen();
    
    // 图像数据包
    image_data_pkt_send();

    // 图像结束包
    image_end_pkt_send();

    // 无线通信关闭
    radio_disable();
}


/*
********************************************************************************
* Function Name : image_resend_checking
* Description   : 图像重发检测
* Parameter     : None
* Return        : None
********************************************************************************
*/
static void image_resend_checking(void)
{
    UINT8* radio_data = radio_receive_one_packet();
	
    // 接收不到应答或应答错误，则重发
    if ( radio_data == NULL
        || radio_data[0] != CMD_IMG_RCVD_RSP
        || radio_data[1] != image_id
        || device_id_is_ok(radio_data + 2) != TRUE)
    {
        image_resend();
    }
	
}


/*
********************************************************************************
* Function Name : image_process
* Description   : 图像处理函数
* Parameter     : None
* Return        : None
********************************************************************************
*/
void image_process(void)
{
    if (TRUE == image_read())
    {
        image_id = (image_id++) % 255;
        image_send();
        image_resend_checking();
    }
}


///*
//********************************************************************************
//* Function Name : capsule_sn_set_checking
//* Description   : 胶囊序列号设定检测
//* Parameter     : None
//* Return        : None
//********************************************************************************
//*/
//void capsule_sn_set_checking(void)
//{
//	 UINT8 *capsule_sn = (UINT8*)FLASH_CAPSULE_SN_ADDR; 
//    
//		if( 0xFF == capsule_sn[0]
//				&& 0xFF == capsule_sn[1]
//				&& 0xFF == capsule_sn[2]
//				&& 0xFF == capsule_sn[3]
//				&& 0xFF == capsule_sn[4]
//				&& 0xFF == capsule_sn[5]
//				&& 0xFF == capsule_sn[6]
//				&& 0xFF == capsule_sn[7])
//		{
//				UINT8* radio_data = radio_receive_one_packet1();
//			  //radio_data = radio_receive_one_packet1();

//				// 接收不到应答或应答错误，则重发
//				if ( radio_data != NULL
//						&& radio_data[0] == CMD_REQ_CAPSULE_SN_SET)
//				{
//					// 无线通信关闭
//					radio_disable();

//					// 擦除之前绑定的序列号
//					flash_page_erase(FLASH_CAPSULE_SN_ADDR);
//					
//					// 写入新序列号
//					flash_buff_write(FLASH_CAPSULE_SN_ADDR, &radio_data[1], 8);
//					
//					// 无线通信使能
//						radio_txen();
//					//胶囊序列号设定应答
//					capsule_sn_set_reply();
//				}
//		 }
//}


