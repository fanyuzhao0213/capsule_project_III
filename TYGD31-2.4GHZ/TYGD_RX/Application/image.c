/*
****************************************************************************
* Copyright(C): TY Technology
* FileName    : image.c
* Description : 图像数据处理
****************************************************************************
*/

#include "image.h"
#include "antenna.h"
#include "capsule_sn_binding.h"
#include "drv_radio.h"
#include "drv_uart.h"
#include "drv_device_id.h"
#include <string.h>



#define IMAGE_BUFF_HEAD_LEN    (7)      // 图像buffer头信息长度
#define IMAGE_BUFF_CHECK_LEN   (1)      // 图像buffer和校验长度

// 图像数据buff空间大小
#define IMAGE_BUFF_SIZE        (IMAGE_BUFF_HEAD_LEN + DEVICE_INFO_SIZE + FRAME_DATA_SIZE + IMAGE_BUFF_CHECK_LEN)

// 图像数据包最大数量
#define IMG_DATA_PKT_AMT_MAX   (256)    



// 图像buffer
static UINT8 image_buff[IMAGE_BUFF_SIZE] = {0};

// 图像数据包接收状态表
static UINT8 packet_rcvd_table[IMG_DATA_PKT_AMT_MAX] = {0};

// 图像数据包数量
static UINT16 img_data_pkt_amt = 0;

//图像数据字节长度
static UINT16 img_data_len = 0;

// 图像帧标识，用于防止乱码
static UINT8 image_id = 0;

// 设备信息buffer
static UINT8 device_info_buff[DEVICE_INFO_SIZE] = {0x00, 0x55, 0xAA, 0x88, 0x99};

// 图像数据和校验
static UINT8 img_data_sum_check = 0;



/*
********************************************************************************
* Function Name : image_rcvd_rsp_pkt_send
* Description   : 发送图像接收完毕应答包
* Parameter     : None
* Return        : None
********************************************************************************
*/
static void image_rcvd_rsp_pkt_send(void)
{
    UINT8 len = 0;
    UINT8 buff[16] = {0};
    
    // 图像接收成功包标识
    buff[len++] = CMD_IMG_RCVD_RSP;

    // 图像帧标识
    buff[len++] = image_id;

    // 胶囊序列号
    buff[len++] = device_info_buff[11];
    buff[len++] = device_info_buff[12];
    buff[len++] = device_info_buff[13];
    buff[len++] = device_info_buff[14];
    buff[len++] = device_info_buff[15];
    buff[len++] = device_info_buff[16];
    buff[len++] = device_info_buff[17];
    buff[len++] = device_info_buff[18];

    // 发送
    radio_send(buff, len);
}


/*
********************************************************************************
* Function Name : image_device_info_cfg
* Description   : 配置图像设备信息
* Parameter     : None
* Return        : None
********************************************************************************
*/
static void image_device_info_cfg(void)
{
    UINT8  i;
    UINT8* device_id = (UINT8*)DEVICE_ID_ADDR;
    
    // RF主版本号
    device_info_buff[7]  = VERSION_MAIN;
    
    // RF子版本号
    device_info_buff[8]  = VERSION_SUB;
    
    // RF序列号
    for (i = 0; i < DEVICE_ID_LEN; i++)
    {
        device_info_buff[19 + i] = device_id[i];
    }
    
    // 天线相关信息
    antenna_read_info(device_info_buff);
    
    // RF测试版本号
    device_info_buff[62]  = VERSION_TEST;

}


/*
********************************************************************************
* Function Name : image_forward
* Description   : 转发图像
* Parameter     : None
* Return        : None
********************************************************************************
*/
static void image_forward(void)
{
    // 如果图像数据字节长度在范围内，则发送图像至上位机
    if (img_data_len <= FRAME_DATA_SIZE)
    {
        UINT16 data_len;
        UINT8  i;
        
        // 图像头信息
        image_buff[0] = 0xFF;
        image_buff[1] = 0x55;
        image_buff[2] = 0x12;
        image_buff[3] = 0x34;
        image_buff[4] = CMD_IMG_FORWARD;
        image_buff[5] = (img_data_len + DEVICE_INFO_SIZE) / 256;
        image_buff[6] = (img_data_len + DEVICE_INFO_SIZE) % 256;

        // 头信息和图像数据长度之和
        data_len = IMAGE_BUFF_HEAD_LEN + img_data_len;

        // 设置RF相关的设备信息
        image_device_info_cfg();
        
        // 拷贝设备信息至图像buffer
        for (i = 0; i < DEVICE_INFO_SIZE; i++)
        {
            image_buff[data_len + i] = device_info_buff[i];
            img_data_sum_check += device_info_buff[i];
        }
        data_len += DEVICE_INFO_SIZE;

        // 和校验
        image_buff[data_len] = img_data_sum_check;
        data_len += IMAGE_BUFF_CHECK_LEN;

        uart_send(image_buff, data_len);             // 通过串口发送图像至PC

        // 更改图像ID，防止接收到重复的图像
        image_id = 0;
    }
}


#ifdef CFG_CAPSULE_SN_BIND
/*
********************************************************************************
* Function Name : image_capsule_sn_check
* Description   : 检测(图像中)胶囊序列号是否正确
* Parameter     : - radio_data: 无线通信数据指针
* Return        : TRUE表示序列号OK，FALSE表示序列号错误
********************************************************************************
*/
static BOOL image_capsule_sn_check(UINT8* radio_data)
{
    UINT8 *capsule_sn = (UINT8*)FLASH_CAPSULE_SN_ADDR; 

    if (TRUE == capsule_sn_is_bound())
    {
        if(radio_data[0] == CMD_IMG_BEGIN_PACKET
            && radio_data[2] == capsule_sn[0]
            && radio_data[3] == capsule_sn[1]
            && radio_data[4] == capsule_sn[2]
            && radio_data[5] == capsule_sn[3]
            && radio_data[6] == capsule_sn[4]
            && radio_data[7] == capsule_sn[5]
            && radio_data[8] == capsule_sn[6]
            && radio_data[9] == capsule_sn[7])
        {
            return TRUE;
        }
        else
        {
            return FALSE;
        }
    }
    else
    {
        return FALSE;
    }
}
#endif


/*
********************************************************************************
* Function Name : image_pkt_check
* Description   : 检测图像包是否正确
* Parameter     : - radio_data: 无线通信数据指针
* Return        : TRUE表示图像包OK，FALSE表示图像包错误
********************************************************************************
*/
static BOOL image_pkt_check(UINT8* radio_data)
{
    UINT8 *capsule_sn = (UINT8*)FLASH_CAPSULE_SN_ADDR; 
	
    if (radio_data[1] == image_id
        && radio_data[2] == capsule_sn[0]
        && radio_data[3] == capsule_sn[1]
        && radio_data[4] == capsule_sn[2]
        && radio_data[5] == capsule_sn[3]
        && radio_data[6] == capsule_sn[4]
        && radio_data[7] == capsule_sn[5]
        && radio_data[8] == capsule_sn[6]
        && radio_data[9] == capsule_sn[7])
    {
        return TRUE;
    }
    else
    {
        return FALSE;
    }
}


/*
********************************************************************************
* Function Name : image_pkt_is_all_received
* Description   : 判断同一帧图像的数据包是否全部接收完毕
* Parameter     : None
* Return        : true表示已全部接收，false表示未全部接收
********************************************************************************
*/
static BOOL image_pkt_is_all_received(void)
{
    UINT16 i;
    
    for(i = 0; i < img_data_pkt_amt; i++)
    {
        if(packet_rcvd_table[i] == 0)
        {
            return FALSE;
        }
    }

    return TRUE;
}


/*
********************************************************************************
* Function Name : image_begin_pkt_parse
* Description   : 解析图像开始包
* Parameter     : - radio_data: 无线通信数据指针
* Return        : None
********************************************************************************
*/
void image_begin_pkt_parse(UINT8* radio_data)
{
#ifdef CFG_CAPSULE_SN_BIND
    if (TRUE == image_capsule_sn_check(radio_data))
#endif
    {        
        // 图像帧标识
        image_id = radio_data[1];

        // 胶囊序列号
        device_info_buff[11] = radio_data[2];
        device_info_buff[12] = radio_data[3];
        device_info_buff[13] = radio_data[4];
        device_info_buff[14] = radio_data[5];
        device_info_buff[15] = radio_data[6];
        device_info_buff[16] = radio_data[7];
        device_info_buff[17] = radio_data[8];
        device_info_buff[18] = radio_data[9];

        // 图像数据长度
        img_data_len = radio_data[10] * 256 + radio_data[11];
        
        // 图像数据包数量
        img_data_pkt_amt = radio_data[12] * 256 + radio_data[13];

        // 胶囊版本号
        device_info_buff[5]  = radio_data[14];
        device_info_buff[6]  = radio_data[15];
        device_info_buff[61] = radio_data[16];
        device_info_buff[68] = radio_data[17];

        // 图像数据包接收状态表复位
        memset(packet_rcvd_table, 0, sizeof(packet_rcvd_table));
    }
}


/*
********************************************************************************
* Function Name : image_data_pkt_parse
* Description   : 解析图像数据包
* Parameter     : - radio_data: 无线通信数据指针
* Return        : None
********************************************************************************
*/
void image_data_pkt_parse(UINT8* radio_data)
{
    UINT16 packet_id, data_len;
    UINT8  *img_buff_addr = NULL, *img_packet_addr = NULL;

    if (TRUE == image_pkt_check(radio_data))
    {
        // 包号
        packet_id = radio_data[10] * 256 + radio_data[11];

        if (packet_id < img_data_pkt_amt && packet_rcvd_table[packet_id] == 0)
        {
            // 拷贝图像数据
            img_buff_addr = image_buff + IMAGE_BUFF_HEAD_LEN + packet_id * PACKET_IMG_DATA_SIZE;
            img_packet_addr = radio_data + (PACKET_SIZE - PACKET_IMG_DATA_SIZE);
            data_len = (packet_id < img_data_pkt_amt - 1) ? (PACKET_IMG_DATA_SIZE) \
					: (img_data_len - (packet_id * PACKET_IMG_DATA_SIZE));
            memcpy(img_buff_addr, img_packet_addr, data_len);

            // 标记对应包号的图像数据包已收到
            packet_rcvd_table[packet_id] = 1;
        }
    }
}


/*
********************************************************************************
* Function Name : image_end_pkt_parse
* Description   : 解析图像结束包
* Parameter     : - radio_data: 无线通信数据指针
* Return        : None
********************************************************************************
*/
void image_end_pkt_parse(UINT8* radio_data)
{
    if (TRUE == image_pkt_check(radio_data))
    {
        if(TRUE == image_pkt_is_all_received())
        {
            img_data_sum_check = radio_data[10];
            
					  // 发送回应包
            radio_disable();
            radio_txen();           
            image_rcvd_rsp_pkt_send();
            image_rcvd_rsp_pkt_send();
            radio_disable();
            radio_rxen();
            
					  // 发送数据到STM32,以及天线检测OK
            image_forward();
            antenna_detect_cnt++;
        }
    }
}


