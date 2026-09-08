/*
****************************************************************************
* Copyright(C): TY Technical
* FileName    : drv_sd2058.c
* Author      : TY Technical Software Development Team
* Description : sd2058时钟芯片底层驱动
****************************************************************************
*/
#include "drv_sd2058.h"
#include "delay.h"

#define SD2058_READ_ADDR		0x65
#define SD2058_WRITE_ADDR		0x64

#define I2C_PORT			GPIOB
#define I2C_SCL_PIN		GPIO_Pin_6
#define I2C_SDA_PIN		GPIO_Pin_7

#define SCL_H         GPIOB->BSRR = I2C_SCL_PIN		//GPIOC->ODR |= I2C_SCL_PIN
#define SCL_L         GPIOB->BRR = I2C_SCL_PIN		//GPIOC->ODR &= (uint8_t)(~I2C_SCL_PIN);
   
#define SDA_H         GPIOB->BSRR = I2C_SDA_PIN		//GPIOC->ODR |= I2C_SDA_PIN
#define SDA_L         GPIOB->BRR = I2C_SDA_PIN		//GPIOC->ODR &= (uint8_t)(~I2C_SDA_PIN);

#define SCL_read      GPIOB->IDR & I2C_SCL_PIN
#define SDA_read      GPIOB->IDR & I2C_SDA_PIN

#define SDA_PUSAL			do {SDA_L; _asm("NOP");_asm("NOP");_asm("NOP");_asm("NOP");_asm("NOP"); SDA_H;} while(0)


// 时钟芯片初始状态
u8 sd2058_begin_state = 0;



/*
********************************************************************************
* Function Name  : I2C_delay
* Description    : iic延时
* Input          : None
* Output         : None
* Return         : None
********************************************************************************
*/
static void I2C_delay(void)
{	
   u8 i = 7; 
   while(i) 
   { 
			i--;
   } 
	
   return;
}


/*
********************************************************************************
* Function Name  : SD2058_I2C_Config
* Description    : SD2058引脚配置
* Input          : None
* Output         : None
* Return         : None
********************************************************************************
*/
static void SD2058_I2C_Config(void)
{
	GPIO_InitTypeDef GPIO_InitStructure; 
	RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOB , ENABLE);

	GPIO_InitStructure.GPIO_Pin = I2C_SDA_PIN;		 
	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_Out_OD;
	GPIO_InitStructure.GPIO_Speed = GPIO_Speed_10MHz;
	GPIO_Init(I2C_PORT, &GPIO_InitStructure);
	
	GPIO_InitStructure.GPIO_Pin = I2C_SCL_PIN;		 
	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_Out_PP;
	GPIO_InitStructure.GPIO_Speed = GPIO_Speed_10MHz;
	GPIO_Init(I2C_PORT, &GPIO_InitStructure);
	
	SDA_H;
	SCL_H;
  
  return;
}


/*
********************************************************************************
* Function Name  : I2C_Start
* Description    : iic开始信号
* Input          : None
* Output         : None
* Return         : None
********************************************************************************
*/
static ErrorStatus I2C_Start(void)
{
	SDA_H;
	SCL_H;
	I2C_delay();
	if(!SDA_read)return ERROR;	// SDA线为低电平则总线忙,退出
	SDA_L;
	I2C_delay();
	if(SDA_read) return ERROR;	// SDA线为高电平则总线出错,退出
	SCL_L;
	I2C_delay();
	return SUCCESS;
}


/*
********************************************************************************
* Function Name  : I2C_Stop
* Description    : iic停止信号
* Input          : None
* Output         : None
* Return         : None
********************************************************************************
*/
static void I2C_Stop(void)
{
	SCL_L;
	I2C_delay();
	SDA_L;
	I2C_delay();
	SCL_H;
	I2C_delay();
	SDA_H;
	I2C_delay();
	I2C_delay();
	SCL_L;
  
  return;
}


/*
********************************************************************************
* Function Name  : I2C_Ack
* Description    : iic应答信号
* Input          : None
* Output         : None
* Return         : None
********************************************************************************
*/
static void I2C_Ack(void)
{	
	SCL_L;
	I2C_delay();
	SDA_L;
	I2C_delay();
	SCL_H;
	I2C_delay();
	SCL_L;
	I2C_delay();
  
  return ;
}


/*
********************************************************************************
* Function Name  : I2C_NoAck
* Description    : iic无应答
* Input          : None
* Output         : None
* Return         : None
********************************************************************************
*/
static void I2C_NoAck(void)
{	
	SCL_L;
	I2C_delay();
	SDA_H;
	I2C_delay();
	SCL_H;
	I2C_delay();
	SCL_L;
	I2C_delay();
  
  return ;
}


/*
********************************************************************************
* Function Name  : I2C_WaitAck
* Description    : iic等待应答
* Input          : None
* Output         : None
* Return         : ERROR无应答/SUCCESS有应答
********************************************************************************
*/
static ErrorStatus I2C_WaitAck(void) 	 //返回为:=1有ACK,=0无ACK
{
	SCL_L;
	I2C_delay();
	SDA_H;			
	I2C_delay();
	SCL_H;
	I2C_delay();
	if(SDA_read)
	{
		SCL_L;
		return ERROR;
	}
	SCL_L;
	return SUCCESS;
}


/*
********************************************************************************
* Function Name  : I2C_SendByte
* Description    : iic发送一个字节
* Input          : SendByte 发送的字节数据
* Output         : None
* Return         : None
********************************************************************************
*/
static void I2C_SendByte(u8 SendByte) //数据从高位到低位//
{
    u8 i=8;
    while(i--)
    {
      SCL_L;
      I2C_delay();
      if(SendByte&0x80)
        SDA_H;  
      else 
        SDA_L;   
      SendByte<<=1;
      I2C_delay();
      SCL_H;
      I2C_delay();
    }
    SCL_L;
}


/*
********************************************************************************
* Function Name  : I2C_ReceiveByte
* Description    : iic接收一个字节
* Input          : None
* Output         : None
* Return         : ReceiveByte 接收数据
********************************************************************************
*/
static u8 I2C_ReceiveByte(void)  //数据从高位到低位//
{ 
    u8 i=8;
    u8 ReceiveByte=0;

    SDA_H;				
    while(i--)
    {
      ReceiveByte<<=1;      
      SCL_L;
      I2C_delay();
			SCL_H;
      I2C_delay();	
      if(SDA_read)
      {
        ReceiveByte|=0x01;
      }
    }
    SCL_L;
    return ReceiveByte;
}


/*
********************************************************************************
* Function Name  : SD2058_ReadOneByte
* Description    : 从SD2058读取以一个字节,并带有应答
* Input          : readAddr 读取地址
* Output         : None
* Return         : retVal   返回读取字节
********************************************************************************
*/
static u8 SD2058_ReadOneByte(u8 readAddr)
{
	u8 retVal;
	//send the read addr
	I2C_Start();
	I2C_SendByte(SD2058_WRITE_ADDR);	
	I2C_WaitAck();
	I2C_SendByte(readAddr & 0x3f);
	I2C_WaitAck();
	//reStart and read
	I2C_Start();
	I2C_SendByte(SD2058_READ_ADDR);
	I2C_WaitAck();
	retVal = I2C_ReceiveByte();
	I2C_Ack();
	I2C_Stop();
	return retVal;
}


/*
********************************************************************************
* Function Name  : SD2058_ReadOneByte
* Description    : 从SD2058指定地址起读取多个字节,无应答
* Input          : readAddr 读取的地址
                   buffer 读取缓存
                   readNum 读取个数 
* Output         : None
* Return         : SUCCESS
********************************************************************************
*/
ErrorStatus SD2058_ReadBytes(u8 readAddr, u8* buffer, u8 readNum)
{
	u8 i;
	
	//send the read addr
	I2C_Start();
	I2C_SendByte(SD2058_WRITE_ADDR);	
	I2C_WaitAck();
	I2C_SendByte(readAddr & 0x3f);
	I2C_WaitAck();
	
	//reStart and read
	I2C_Start();
	I2C_SendByte(SD2058_READ_ADDR);
	I2C_WaitAck();
	for(i = 0; i < readNum; i++)
	{
		buffer[i] = I2C_ReceiveByte();
		if(i < (readNum - 1))
			I2C_Ack();
	}
	I2C_NoAck();
	I2C_Stop();
	return SUCCESS;
}


/*
********************************************************************************
* Function Name  : SD2058_ReadBytes_From_Begin
* Description    : 从SD2058读取多个字节,无应答
* Input          : buffer  读取缓存
                   readNum 读取数据个数
* Output         : None
* Return         : SUCCESS
********************************************************************************
*/
ErrorStatus SD2058_ReadBytes_From_Begin(u8* buffer, u8 readNum)
{
	u8 i;
	
	//send the read addr
	I2C_Start();
	I2C_SendByte(SD2058_READ_ADDR);
	I2C_WaitAck();
	for(i = 0; i < readNum; i++)
	{
		buffer[i] = I2C_ReceiveByte();
		if(i < (readNum - 1))
			I2C_Ack();
	}
	I2C_NoAck();
	I2C_Stop();
	return SUCCESS;
}


/*
********************************************************************************
* Function Name  : SD2058_Write_OneByte
* Description    : 写入SD2058一个字节
* Input          : writeAddr  写入的地址
                   data       写入数据
* Output         : None
* Return         : ERROR写入失败/SUCCESS写入成功
********************************************************************************
*/
ErrorStatus SD2058_Write_OneByte(u8 writeAddr, u8 data)
{
	//send the read addr
	I2C_Start();
	I2C_SendByte(SD2058_WRITE_ADDR);	
	I2C_WaitAck();
	I2C_SendByte(writeAddr & 0x3f);
	//I2C_WaitAck();
	if(!I2C_WaitAck())
	{
		I2C_Stop(); 
		return ERROR;
	}
	I2C_SendByte(data);
	
	if(!I2C_WaitAck())
	{
		I2C_Stop(); 
		return ERROR;
	}
	
	I2C_Stop();
	return SUCCESS;
}


/*
********************************************************************************
* Function Name  : SD2058_Write_Bytes
* Description    : 写入SD2058多个字节
* Input          : writeAddr  写入的地址
                   buffer     写入数据
                   writeNum   写入个数
* Output         : None
* Return         : ERROR写入失败/SUCCESS写入成功
********************************************************************************
*/
ErrorStatus SD2058_Write_Bytes(u8 writeAddr, u8* buffer, u8 writeNum)
{
	u8 i;
	
	//send the read addr
	I2C_Start();
	I2C_SendByte(SD2058_WRITE_ADDR);	
	if(!I2C_WaitAck())
	{
		I2C_Stop(); 
		return ERROR;
	}
	I2C_SendByte(writeAddr & 0x3f);
	if(!I2C_WaitAck())
	{
		I2C_Stop(); 
		return ERROR;
	}

	for(i = 0; i < writeNum; i++)
	{
		//buffer[i] = ((buffer[i] / 10) << 4) + (buffer[i] % 10);
		I2C_SendByte(buffer[i]);
		I2C_WaitAck();
	}
	I2C_Stop();
	return SUCCESS;
}


/*
********************************************************************************
* Function Name  : Enable_Write_Protect
* Description    : 使能写保护
* Input          : None
* Output         : None
* Return         : None
********************************************************************************
*/
void Enable_Write_Protect(void)
{
	u8 ctr1, ctr2;
	SD2058_ReadBytes(0x0f, &ctr1, 1);
	SD2058_ReadBytes(0x10, &ctr2, 1);
	ctr1 |= 0x84;	//WRTC2 | WRTC3
	ctr2 |= 0x80;	//WRTC1
	SD2058_Write_Bytes(0x10, &ctr2, 1);//WRTC1 write first 
	SD2058_Write_Bytes(0x0f, &ctr1, 1);
  
  return ;
}


/*
********************************************************************************
* Function Name  : Disable_Write_Protect
* Description    : 失能写保护
* Input          : None
* Output         : None
* Return         : None
********************************************************************************
*/
void Disable_Write_Protect(void)
{
	u8 ctr1, ctr2;
	SD2058_ReadBytes(0x0f, &ctr1, 1);
	SD2058_ReadBytes(0x10, &ctr2, 1);
	ctr1 &= 0x7b;	//WRTC2 | WRTC3
	ctr2 &= 0x7f;	//WRTC1
	SD2058_Write_Bytes(0x0f, &ctr1, 1);//WRTC2 | WRTC3 write first
	SD2058_Write_Bytes(0x10, &ctr2, 1);
  
  return ;
}


/*
********************************************************************************
* Function Name  : SD2058_Init
* Description    : SD2058初始化  时钟芯片
* Input          : None
* Output         : None
* Return         : None
********************************************************************************
*/
void SD2058_Init(void)
{
	u8 tmpData;
//	u8 hmr[3] = {0x59,0x59,0x23};//23:59:59
	u8 dataTim[7];
	u8 i;
	
	SD2058_I2C_Config();//初始化接口
	
	for (i=0; i<30; i++)
	{
		tmpData = SD2058_ReadOneByte(0x0F);//
		if (tmpData == 0x00)
		{
			break;
		}	
		delay_ms(30);
	}

	// i==30防止掉电重新上电会出现随机时间 目前测试的最多需要读取的次数为17次
	if((tmpData & 0x1) || (i==30))//RTCF位
	{
		Enable_Write_Protect();
		
//		//设置24小时制
//		tmpData = SD2058_ReadOneByte(0x02);
//		tmpData |= 0x80;
//		SD2058_Write_OneByte(0x02, tmpData);
		
//		//报警允许设置：每天23:59:59(hour, minite, secend)
//		SD2058_Write_Bytes(0x07, hmr, 3);
//		SD2058_Write_OneByte(0x0e, 0x07);//选择报警输出,单脉冲输出
//		SD2058_Write_OneByte(0x10, 0x92);//INTS1 INTS0:01, INTAE:1,WRTC1:1.
//		SD2058_Write_OneByte(0x11, 0xc0);//CTR3-ARST: 1-1
		
//		//频率报警
//		SD2058_Write_OneByte(0x10, 0xA1);//INTS1 INTS0:10, INTFE:1,WRTC1:1. 频率中断
//    SD2058_Write_OneByte(0x11, 0x0F);//FS3-FS0: 1-1，秒频率中断
		
		//设置初始时间 2000/01/01 00:00:00	
		dataTim[0] = 0x00;						// second
		dataTim[1] = 0x00;						// min
		dataTim[2] = 0x00 | 0x80;			// hour and 24hours system
		dataTim[3] = 0x06;			      // wday
		dataTim[4] = 0x01;						// mday
		dataTim[5] = 0x01;						// mon
		dataTim[6] = 0x00;						// year
		SD2058_Write_Bytes(0x00, dataTim, 7);
		
		Disable_Write_Protect();
		
		sd2058_begin_state = 0x01;
	}
  
  return;
}

