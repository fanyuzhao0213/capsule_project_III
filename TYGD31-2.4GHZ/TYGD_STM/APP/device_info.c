/*
****************************************************************************
* Copyright(C): TY Technical
* FileName    : device_info.c
* Author      : TY Technical Software Development Team
* Description : 通信数据/信息
****************************************************************************
*/
#include "device_info.h"

#include "drv_sd2058.h"
#include "time_handle.h"

// MCU芯片型号序列号内存地址
static u8 *MCU_id = (u8 *)0x1FFFF7E8;

/*
********************************************************************************
* Function Name  : MCU_info_copy
* Description    : MCU芯片ID获取
* Input          : None
* Output         : None
* Return         : None
********************************************************************************
*/
static void MCU_info_copy(u8 *buff_info)
{
  // 检查输入参数
  if (NULL == buff_info)
  {
    return ;
  }
  
  // 获取stm32ID号信息 
  buff_info[27] = MCU_id[0];
  buff_info[28] = MCU_id[1];
  buff_info[29] = MCU_id[2];
  buff_info[30] = MCU_id[3];
  buff_info[31] = MCU_id[4];
  buff_info[32] = MCU_id[5];
  buff_info[33] = MCU_id[6];
  buff_info[34] = MCU_id[7];
  buff_info[35] = MCU_id[8];
  buff_info[36] = MCU_id[9];
  buff_info[37] = MCU_id[10];
  buff_info[38] = MCU_id[11];
  
  return ;
}


/*
********************************************************************************
* Function Name  : version_info_copy
* Description    : 版本信息
* Input          : buff_info
* Output         : None
* Return         : None
********************************************************************************
*/
static void version_info_copy(u8 *buff_info)
{
  // 检查输入参数
  if (NULL == buff_info)
  {
    return ;
  }
  
	// 主版本号
  buff_info[9]=VERSION_MAIN;
	
  // 子版本
  buff_info[10]=VERSION_SUB;
  
  // 测试版本
  buff_info[63]=VERSION_TEST; 
	
  return ;
}


/*
********************************************************************************
* Function Name  : data_head_info_copy
* Description    : 数据头信息
* Input          : buff_info
* Output         : None
* Return         : None
********************************************************************************
*/
static void data_head_info_copy(u8 *buff_info)
{
  // 检查输入参数
  if (NULL == buff_info)
  {
    return ;
  }
  
  buff_info[0] = 0x00;
  buff_info[1] = 0x55;
  buff_info[2] = 0xaa;
  buff_info[3] = 0x88;
  buff_info[4] = 0x99;
  
  return ;
}  


/*


********************************************************************************
* Function Name  : device_info_rewrite
* Description    : 重写MCU发送到PC端显示的信息,包含软件版本 软件序列号等
* Input          : buff_info
* Output         : None
* Return         : None
********************************************************************************
*/
void device_info_rewrite(u8 *buff_info)
{
  data_head_info_copy(buff_info);
  version_info_copy(buff_info);
  MCU_info_copy(buff_info);
  
  return ;
}

