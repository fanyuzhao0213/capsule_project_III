/*
****************************************************************************
* Copyright(C): TY Technical
* FileName    : drv_usart.c
* Author      : TY Technical Software Development Team
* Description : 串口驱动
****************************************************************************
*/
#include "drv_usart.h"
#include "receive_send_image.h"
#include "time_handle.h"
#include "jpeg_head.h"
#include "storage_card.h"
#include "system_stm32_tick.h"
#include "usart_transfer_station.h"
#include <string.h>
#include "do_main.h"

#include "calendar.h"


// 通信协议头标识
static const u8 Comm_protocol_head[4] = {0xFF, 0x55, 0x12, 0x34};

u8 Usart1_rec_buf[300] = {0};

vu8  rx_step2  = 0;

/*
********************************************************************************
* Function Name  : USART3_Configuration
* Description    : Configures the USART3. 主控板与PC通信
* Input          : None
* Output         : None
* Return         : None
********************************************************************************
*/
void USART3_Configuration(void)
{   
  GPIO_InitTypeDef GPIO_InitStructure;
  USART_InitTypeDef USART_InitStructure; 
  NVIC_InitTypeDef NVIC_InitStructure; 

	RCC_APB1PeriphClockCmd(RCC_APB1Periph_USART3, ENABLE);
  RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOB | RCC_APB2Periph_AFIO,ENABLE);    //?????? USART1????,????????
	
  GPIO_InitStructure.GPIO_Pin =  GPIO_Pin_10;
  GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
  GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AF_PP;//
  GPIO_Init(GPIOB, &GPIO_InitStructure);     

  GPIO_InitStructure.GPIO_Pin =  GPIO_Pin_11;
  GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
  GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IN_FLOATING;//
  GPIO_Init(GPIOB, &GPIO_InitStructure);     

  USART_InitStructure.USART_BaudRate = 921600;//
  USART_InitStructure.USART_WordLength = USART_WordLength_8b;//
  USART_InitStructure.USART_StopBits = USART_StopBits_1;
  USART_InitStructure.USART_Parity = USART_Parity_No;
  USART_InitStructure.USART_HardwareFlowControl = USART_HardwareFlowControl_None; //
  USART_InitStructure.USART_Mode = USART_Mode_Rx | USART_Mode_Tx; //
  USART_Init(USART3, &USART_InitStructure); //

  // 开启中断(发送和接收)
  USART_ITConfig(USART3, USART_IT_RXNE, ENABLE);

  USART_ClearFlag(USART3, USART_FLAG_RXNE);
  
  USART_Cmd(USART3, ENABLE);

  NVIC_InitStructure.NVIC_IRQChannel = USART3_IRQn;           //
  NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 1;   //
  NVIC_InitStructure.NVIC_IRQChannelSubPriority = 0;          //
  NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;             //
  NVIC_Init(&NVIC_InitStructure);
}


/*
********************************************************************************
* Function Name  : USART2_Configuration
* Description    : Configures the USART2. 主控板与RF通信
* Input          : None
* Output         : None
* Return         : None
********************************************************************************
*/
void USART2_Configuration(void) 
{ 	
	GPIO_InitTypeDef GPIO_InitStructure;
	USART_InitTypeDef USART_InitStructure;
	NVIC_InitTypeDef NVIC_InitStructure;

	/* Enable USARTx clock */
	RCC_APB1PeriphClockCmd(RCC_APB1Periph_USART2, ENABLE);
	RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA | RCC_APB2Periph_AFIO, ENABLE);
		
	/* Configure USART2 Tx (PA.2)  */
	GPIO_InitStructure.GPIO_Pin = GPIO_Pin_2;
	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AF_PP;
	GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
	GPIO_Init(GPIOA, &GPIO_InitStructure);

	/* Configure USART2 Rx (PA.3) as input floating */
	GPIO_InitStructure.GPIO_Pin = GPIO_Pin_3;
	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IN_FLOATING;
	GPIO_Init(GPIOA, &GPIO_InitStructure);

	USART_InitStructure.USART_BaudRate = 1000000;//921600
	USART_InitStructure.USART_WordLength = USART_WordLength_8b;
	USART_InitStructure.USART_StopBits = USART_StopBits_1;
	USART_InitStructure.USART_Parity = USART_Parity_No;
	USART_InitStructure.USART_Mode = USART_Mode_Rx | USART_Mode_Tx;
	USART_InitStructure.USART_HardwareFlowControl = USART_HardwareFlowControl_None;

	USART_Init(USART2, &USART_InitStructure);

  // 开启中断(发送和接收)
	USART_ITConfig(USART2, USART_IT_RXNE, ENABLE);
  
  USART_ClearFlag(USART2, USART_FLAG_RXNE);
  
	/* Enable USART2 */
	USART_Cmd(USART2, ENABLE);
  
  /* Configure the NVIC Preemption Priority Bits */
  NVIC_InitStructure.NVIC_IRQChannel = USART2_IRQn;
  NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 2;
  NVIC_InitStructure.NVIC_IRQChannelSubPriority = 0;
  NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;
  NVIC_Init(&NVIC_InitStructure);
}


/*
********************************************************************************
* Function Name : USART3_Receive
* Description   : 串口数据接收
* Paramter      : rx_data: 数据接收rx_data
* Return        : 返回接收到的数据长度
********************************************************************************
*/
static void USART3_Receive(u8 rx_data)
{
  static vu8  rx_cmd   = 0;
  static vu8  rx_step3 = 0;
  static vu16 rx_index = 0;
  static vu16 rx_data_len  = 0;
  static vu8  rx_sum_check = 0;
  static vu8  rx_cnt = 0;
  
  if (++rx_cnt > 1024UL)
  {
    rx_step3 = 0;
		rx_sum_check = 0;
    rx_cnt = 0;
  }
  
  switch (rx_step3)
  {
    case 0:
    case 1:
    case 2:
    case 3:
        if (rx_data == Comm_protocol_head[rx_step3])
        {
          rx_step3++;
        }
        else
        {
          rx_step3 = 0;
        }
      break;
    case 4:
      rx_cmd = rx_data;
      rx_step3++;
      break;
    case 5:
      rx_data_len = rx_data << 8;
      rx_step3++;
      break;
    case 6:
      rx_data_len += rx_data;
    
      // 数据长度过长则放弃本次接收,超过范围会导致数组溢出
      if (rx_data_len < 300)
      {
        rx_index = 0;
        
        // 协议中有些命令没有数据
        rx_step3 = (rx_data_len == 0) ? (rx_step3 + 2) : (rx_step3 + 1);
      }
      else
      {
        rx_step3 = 0;
      }
			
			rx_sum_check = 0;
      break;
    case 7:  // 数据			
				Usart1_rec_buf[rx_index++] = rx_data;		
				
        rx_sum_check += rx_data;
        if (rx_index == rx_data_len)
        {
          rx_step3++;
        }	
      break;
    case 8:
      if (rx_data == rx_sum_check)
      {
				switch (rx_cmd)
				{
					case CMD_PC_TO_TIME:
							Rx_time_flinsh = 1; 
							// 为下一次更新时间准备
							Rx_time_flinsh = 0;
							// 从上位机获取当前电脑时间,设置RTC时间
							BcdSetRtcTime(Usart1_rec_buf);		
					    // 设置RTC时间
						// 没有图像数据不用读取
						if(!Detect_Bind_Capsule_File())//		
						 {
							DeviceInfo[83] = Usart1_rec_buf[0];
							DeviceInfo[84] = Usart1_rec_buf[1];
							DeviceInfo[85] = Usart1_rec_buf[2];
							DeviceInfo[86] = Usart1_rec_buf[3];
							DeviceInfo[87] = Usart1_rec_buf[4];
							DeviceInfo[88] = Usart1_rec_buf[5];
							DeviceInfo[89] = Usart1_rec_buf[6];		
						 }						 
							// 对Usart1_rec_buf清零
							memset(Usart1_rec_buf, 0, sizeof(Usart1_rec_buf));
					
						break;
					case CMD_REQ_CAPSULE_SN_QUERY:  // 已绑定胶囊序列号查询请求
							Capsule_ReqAckOKFlag = 0x20;
							Capsule_process_flag = CASE_REQ_CAPSULE_SN;
							Capsule_DataLen = rx_data_len;
							break;                 
					case CMD_REQ_CAPSULE_SN_UNBIND: // 胶囊序列号解绑请求
							Capsule_ReqAckOKFlag = 0x22;
							Capsule_process_flag = CASE_REQ_CAPSULE_SN;
							Capsule_DataLen = rx_data_len;
					    //文件创建标志清零
					    Capsule_File_FLAG = FALSE;
							break;                          
					case CMD_REQ_CAPSULE_SN_BIND:   // 胶囊序列号绑定请求
							Capsule_ReqAckOKFlag = 0x24;
							Capsule_process_flag = CASE_REQ_CAPSULE_SN;
							Capsule_DataLen = rx_data_len;	
					    break;
          case CMD_REQ_CAPSULE_SN_SET:   // 胶囊序列号设定请求
							Capsule_ReqAckOKFlag = 0x26;
							Capsule_process_flag = CASE_REQ_CAPSULE_SN;
							Capsule_DataLen = rx_data_len;					
							break;  
					case CMD_REQ_CAPSULE_SN_SET_OK:   // 胶囊序列号设定请求
							Capsule_ReqAckOKFlag = 0x28;
							Capsule_process_flag = CASE_REQ_CAPSULE_SN;
							Capsule_DataLen = rx_data_len;					
							break;  
					case CMD_REQ_RADIO_CHANNEL_FREQ_SET:   // 无线通信频道设定请求
							Capsule_ReqAckOKFlag = 0x2A;
							Capsule_process_flag = CASE_REQ_CAPSULE_SN;
							Capsule_DataLen = rx_data_len;					
							break; 
					default: break;					
				}
      }
			
			rx_data_len = 0;
			rx_index = 0;
			rx_step3 = 0;
			rx_cnt = 0;
			
      break;
			
			default: 
				rx_data_len = 0;
				rx_index = 0;
				rx_step3 = 0;
				rx_cnt = 0;	
			break;
  }
  
  return ;
}


/*
********************************************************************************
* Function Name : enable_usart3_tx
* Description   : 使能串口发送
* Paramter      : None
* Return        : None
********************************************************************************
*/
static void enable_usart3_tx(void)
{
	USART3->CR1 |= USART_Mode_Tx;
}


/*
********************************************************************************
* Function Name : disable_usart3_tx
* Description   : 失能串口发送
* Paramter      : None
* Return        : None
********************************************************************************
*/
static void disable_usart3_tx(void)
{
	USART3->CR1 &= ~(u16)(USART_Mode_Tx);
}


/*
********************************************************************************
* Function Name : USART3_Send
* Description   : 串口数据发送数据
* Paramter      : - buff: 数据buf
*                 - len : 数据长度
* Return        : None
********************************************************************************
*/
void USART3_Send(u8 cmd, u16 data_len, u8 *u1_tx_buff)
{
  u16 i = 0;
  u8 usart1_crc = 0;
  u8 head_data[7];
  
  head_data[0]=Comm_protocol_head[0];
	head_data[1]=Comm_protocol_head[1];
	head_data[2]=Comm_protocol_head[2];
	head_data[3]=Comm_protocol_head[3];
	head_data[4]=cmd;
	head_data[5]=(u8)(data_len>>8);
	head_data[6]=(u8)data_len;
  
  enable_usart3_tx();
  
  // 发送头帧
  for (i=0; i<7; i++)
  {
    USART3->DR = (head_data[i] & (uint16_t)0x01FF); // 为了加快速度,不使用函数
    while (!(USART3->SR & USART_FLAG_TXE));
  }
  
  // 发送数据
  for (i=0; i<data_len; i++)
  {
    USART3->DR = (u1_tx_buff[i] & (uint16_t)0x01FF); // 为了加快速度,不使用函数
    usart1_crc += u1_tx_buff[i];
    while (!(USART3->SR & USART_FLAG_TXE));
  }
  
  //发送校验
  USART3->DR = (usart1_crc & (uint16_t)0x01FF);//为了加快速度
  while (!(USART3->SR & USART_FLAG_TXE));
  
  // 清空传输标志位
  while (!(USART3->SR & USART_FLAG_TC));
  
  disable_usart3_tx();
  
  USART_ClearFlag(USART3, USART_FLAG_RXNE);
}


/*
********************************************************************************
* Function Name : USART1_IRQHandler
* Description   : 串口1中断
* Paramter      : None
* Return        : None
********************************************************************************
*/
void USART3_IRQHandler(void)
{  
    // 接收中断
    if(USART_GetITStatus(USART3, USART_IT_RXNE) != RESET)
    {
			u8 rx1_data = USART3->DR;
		
			USART_ClearITPendingBit(USART3, USART_IT_RXNE);
		
			USART3_Receive(rx1_data);  		
    }   
		
		// 清楚串口接收溢出标志，防止比它优先级高的中断打断之后出现异常  
    if (USART_GetITStatus(USART3, USART_IT_ORE_RX) != RESET)
    {
        USART_ClearITPendingBit(USART3, USART_IT_ORE_RX);
        (void)USART3->DR;
    }
}


/*
********************************************************************************
* Function Name : USART2_Receive
* Description   : 串口数据接收
* Paramter      : - buff: 数据接收buff
* Return        : 返回接收到的数据长度
********************************************************************************
*/
void USART2_Receive(u8 rx_buff)
{
  static vu8  rx_cmd   = 0;
  static vu32 rx_cnt   = 0;
  static vu16 rx_index = 0;
  static vu16 rx_data_len  = 0;
  static vu8  rx_sum_check = 0;
  
  if (++rx_cnt > (1024UL * 40UL)) // 这个数要大于等于1张图片的大小
  {
    rx_step2 = 0;
    rx_cnt = 0;
		rx_sum_check = 0;
  }
  
  switch (rx_step2)
  {
    case 0:
    case 1:
    case 2:
    case 3:
        if (rx_buff == Comm_protocol_head[rx_step2])
        {
          rx_step2++;
        }
        else
        {
          rx_step2 = 0;
        }
      break;
    case 4:
      rx_cmd = rx_buff;
      rx_step2++;
      break;
    case 5:
      rx_data_len = rx_buff << 8;
      rx_step2++;
      break;
    case 6:
      rx_data_len += rx_buff;
    
      // 数据长度过长则放弃本次图片接收
      if (rx_data_len < BUFF_SIZE)
      {
        rx_index = 0;
        
				// 图像长度超范围
				if (rx_data_len > 19378UL)
				{
					Frame_out_range = 1;
				}
				
        // 协议中有些命令没有数据
        rx_step2 = (rx_data_len == 0) ? (rx_step2 + 2) : (rx_step2 + 1);
      }
      else
      {
        rx_step2 = 0;
      }	

			rx_sum_check = 0;
			
      break;
    case 7:  // 数据			
			switch (rx_cmd)
			{
				case CMD_IMG_FORWARD:
						if (rx_index > 1)
						{
							Rec_Buf[FRAME_HEADER_SIZE + IMAGE_HEADER_LEN + (rx_index - 2)] = rx_buff;
						}
						break;
				case CMD_RSP_CAPSULE_SN_QUERY:
				case CMD_RSP_CAPSULE_SN_UNBIND:
				case CMD_RSP_CAPSULE_SN_BIND:
				case CMD_RSP_CAPSULE_SN_SET:
				case CMD_CAPSULE_SN_BROADCAST: 
        case CMD_CAPSULE_ID_BROADCAST:
        case CMD_RSP_CAPSULE_SN_SET_OK:		
        case CMD_RSP_RADIO_CHANNEL_FREQ_SET:					
						// 限制一下数组，防止溢出
						if (rx_index < 20)
						{
							Capsule_Ack[rx_index] = rx_buff;
						}
					break;								
				default: break;
			}
     
      rx_sum_check += rx_buff;
      rx_index++;
      if (rx_index == rx_data_len)
      {
        rx_step2++;
      }
      break;
    case 8:
      if (rx_buff == rx_sum_check)
      {            
				switch (rx_cmd)
				{
					case CMD_IMG_FORWARD:
						Send_data_len = rx_data_len;
						
						// 图片数据接收完成标志 
						Img_data_reday = 1;
					
						break;
					case CMD_RSP_CAPSULE_SN_QUERY:
						Capsule_ReqAckOKFlag = 0x21;
						Capsule_process_flag = CASE_RSP_CAPSULE_SN;
						Capsule_DataLen = rx_data_len;
						break;
					case CMD_RSP_CAPSULE_SN_UNBIND:
						Capsule_ReqAckOKFlag = 0x23;
						Capsule_process_flag = CASE_RSP_CAPSULE_SN;
						Capsule_DataLen = rx_data_len;
						break;
					case CMD_RSP_CAPSULE_SN_BIND:
						Capsule_ReqAckOKFlag = 0x25;
						Capsule_process_flag = CASE_RSP_CAPSULE_SN;
						Capsule_DataLen = rx_data_len;
						break;
					case CMD_RSP_CAPSULE_SN_SET:
						Capsule_ReqAckOKFlag = 0x27;
						Capsule_process_flag = CASE_RSP_CAPSULE_SN;
						Capsule_DataLen = rx_data_len;
						break;
					case CMD_CAPSULE_SN_BROADCAST:
						Capsule_ReqAckOKFlag = 0x05;
						Capsule_process_flag = CASE_RSP_CAPSULE_SN;
						Capsule_DataLen = rx_data_len;
						break;	
          case CMD_CAPSULE_ID_BROADCAST:
						Capsule_ReqAckOKFlag = 0x07;
						Capsule_process_flag = CASE_RSP_CAPSULE_SN;
						Capsule_DataLen = rx_data_len;
						break;	
          case CMD_RSP_CAPSULE_SN_SET_OK:
						Capsule_ReqAckOKFlag = 0x29;
						Capsule_process_flag = CASE_RSP_CAPSULE_SN;
						Capsule_DataLen = rx_data_len;
						break;	
          case CMD_RSP_RADIO_CHANNEL_FREQ_SET:
						Capsule_ReqAckOKFlag = 0x2B;
						Capsule_process_flag = CASE_RSP_CAPSULE_SN;
						Capsule_DataLen = rx_data_len;
						break;											
					default: break;
				}  
      }
			
			rx_data_len = 0; // 这个可以不清零
			rx_index = 0;
			rx_step2 = 0;
			rx_cnt = 0;
			
      break;
			
		default: 
			rx_data_len = 0; // 这个可以不清零
			rx_index = 0;
			rx_step2 = 0;
			rx_cnt = 0;
		break;
  } 
	
  return ;
}


/*
********************************************************************************
* Function Name : enable_usart2_tx
* Description   : 使能串口发送
* Paramter      : None
* Return        : None
********************************************************************************
*/
static void enable_usart2_tx(void)
{
	USART2->CR1 |= USART_Mode_Tx;
}


/*
********************************************************************************
* Function Name : disable_usart2_tx
* Description   : 失能串口发送
* Paramter      : None
* Return        : None
********************************************************************************
*/
static void disable_usart2_tx(void)
{
	USART2->CR1 &= ~(u16)(USART_Mode_Tx);
}

/*
********************************************************************************
* Function Name : USART2_Send
* Description   : 串口数据发送数据
* Paramter      : - buff: 数据buf
*                 - len : 数据长度
*                 - cmd : 数据命令
* Return        : None
********************************************************************************
*/
void USART2_Send(u8 cmd, u16 data_len, u8 *u2_tx_buff)
{
  u16 i = 0;
  u8 usart2_crc = 0;
  u8 head_data[7];
  
  head_data[0]=Comm_protocol_head[0];
	head_data[1]=Comm_protocol_head[1];
	head_data[2]=Comm_protocol_head[2];
	head_data[3]=Comm_protocol_head[3];
	head_data[4]=cmd;
	head_data[5]=(u8)(data_len>>8);
	head_data[6]=(u8)data_len;
  
  enable_usart2_tx();
  
  // 发送头帧
  for (i=0; i<7; i++)
  {
    USART2->DR = (head_data[i] & (uint16_t)0x01FF); // 为了加快速度,不使用函数
    while (!(USART2->SR & USART_FLAG_TXE));
  }
  
  // 发送数据
  for (i=0; i<data_len; i++)
  {
    USART2->DR = (u2_tx_buff[i] & (uint16_t)0x01FF); // 为了加快速度,不使用函数
    usart2_crc += u2_tx_buff[i];
    while (!(USART2->SR & USART_FLAG_TXE));
  }
  
  //发送校验
  USART2->DR = (usart2_crc & (uint16_t)0x01FF);//为了加快速度
  while (!(USART2->SR & USART_FLAG_TXE));
  
  // 清空传输标志位
  while (!(USART2->SR & USART_FLAG_TC));
  
  disable_usart2_tx();
  
  USART_ClearFlag(USART2, USART_FLAG_RXNE);
  
  return;
}


/*
********************************************************************************
* Function Name : USART2_IRQHandler
* Description   : 串口2中断
* Paramter      : None
* Return        : None
********************************************************************************
*/
void USART2_IRQHandler(void)
{
    // 接收中断
    if(USART_GetITStatus(USART2, USART_IT_RXNE) != RESET)
    {
        u8 rx_data = USART2->DR;
      
        USART_ClearITPendingBit(USART2, USART_IT_RXNE);
      
				if (!Img_process_finsh)
				{
					USART2_Receive(rx_data);
				}
    }
		
		//	清楚串口接收溢出标志，防止比它优先级高的中断打断之后出现异常
		if (USART_GetITStatus(USART2, USART_IT_ORE_RX) != RESET)
    {
        USART_ClearITPendingBit(USART2, USART_IT_ORE_RX);
        (void)USART2->DR;
    }
}


