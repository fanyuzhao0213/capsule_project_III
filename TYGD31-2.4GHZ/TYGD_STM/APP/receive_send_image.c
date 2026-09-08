/*
****************************************************************************
* Copyright(C): TY Technical
* FileName    : receive_send_image.c
* Author      : TY Technical Software Development Team
* Description : 从RF接收图像数据,发送到PC端/存储到SD卡
****************************************************************************
*/
#include "receive_send_image.h"
#include "device_info.h"
#include "drv_usart.h"
#include "jpeg_head.h"
#include "time_handle.h"

// 接收数据指针<-RF(参考点为ZKB)
u8* Rec_Buf;

// 发送数据指针->PC(参考点为ZKB)
u8* Send_Buf;

// 设备信息存储数组
u8 DeviceInfo[DEVICE_INFO_LEN] = {0};

// 图片数据已经接收和处理完成
u8 Img_data_reday = 0;

// 发送的数据长度
u32 Send_data_len = 0;

// 图像是否处理完成
u8 Img_process_finsh = 0;


/*
********************************************************************************
* Function Name  : device_info_handle
* Description    : 复制rec_buf最后DEVICE_INFO_LEN长度数据到DeviceInfo[]中
* Input          : None
* Output         : None
* Return         : None
********************************************************************************
*/
static void device_info_handle(u8 *rec_buf)
{
  u8 i;
  u32 rec_buf_num = 0;
  
  // 检查输入参数
  if (NULL == rec_buf)
  {
    return ;
  }
  
  rec_buf_num = Send_data_len;
  
  // 丢弃 Rec_Buf的FF D8
  rec_buf_num += IMAGE_HEADER_LEN;
  rec_buf_num += FRAME_HEADER_SIZE; 
  rec_buf_num -= 2;
  
//  // 获取设备信息
//  for (i=DEVICE_INFO_LEN; i>0; i--)
//  {
//    DeviceInfo[i] = rec_buf[rec_buf_num--];
//  }
  	
	//获取有效的设备信息
	rec_buf_num = rec_buf_num-64;
  for (i=DEVICE_INFO_LEN-64; i>0; i--)
  {
    DeviceInfo[i] = rec_buf[rec_buf_num--];
  }
  // 重写DeviceInfo
  device_info_rewrite(DeviceInfo);
  
  return ;
}

/*
********************************************************************************
* Function Name  : receive_image_data_end
* Description    : 接收图片数据结束(校验通过,图片接收完成)
* Input          : None
* Output         : None
* Return         : None
********************************************************************************
*/
BOOL receive_image_data_end(void)
{
  // 图片数据还没接收完成
  if (!Img_data_reday)
  {
    return FALSE;
  }
	
  // 设备信息处理
  device_info_handle(Rec_Buf);
   
  // 对发送PC端的数据赋值
  Send_Buf = Rec_Buf;
  
  return TRUE;
}


/*
********************************************************************************
* Function Name  : send_img_to_PC
* Description    : 发送图片数据到PC端
* Input          : cmd 发送的命令
* Output         : None
* Return         : None
********************************************************************************
*/
void send_image_to_PC(u8 cmd)
{
	u16 i;  
	
	// 数据加上jpeg格式长度和设备信息长度
	Send_data_len += IMAGE_HEADER_LEN;
	Send_data_len += FRAME_HEADER_SIZE; 
	
	// 拷贝设备信息到发送缓存
	for (i=0; i<FRAME_HEADER_SIZE; i++)
	{
		Send_Buf[Send_data_len + i] = DeviceInfo[i];
	} 
	
	// 串口3发送数据
	USART3_Send(cmd, Send_data_len, &Send_Buf[FRAME_HEADER_SIZE]); 
	
	// 清除图像接收完成标志
  Img_data_reday = 0;
  
  // 更新图像数据长度
  Send_data_len = 0;
		
  return ;
}

