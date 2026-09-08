/*
****************************************************************************
* Copyright(C): TY Technology
* FileName    : dev_rf1662.c
* Description : 配置rf1662
****************************************************************************
*/

#include "dev_rf1662.h"
#include "drv_gpio.h"



#define RF1662_SCK_PIN                (19u)//(8u)    // 时钟引脚
#define RF1662_SDA_PIN                (20u)//(7u)    // 数据引脚

#define RF1662_SLAVE_ADDR             (0x0A)  // RF1662从设备地址

// NRF1662时钟延时 N us
#define RF1662_DELAY_N                (2)

// NRF1662时钟延时 M us
#define RF1662_DELAY_M                (RF1662_DELAY_N * 6)  



/*
********************************************************************************
* Function Name : RF1662_GPIO_Init
* Description   : RF1662引脚初始化
* Parameter     : None
* Return        : None
********************************************************************************
*/
static void RF1662_GPIO_Init(void)
{
    gpio_cfg_output(RF1662_SCK_PIN);
    gpio_cfg_output(RF1662_SDA_PIN);
    
    gpio_pin_write(RF1662_SCK_PIN, PIN_LOW);
    gpio_pin_write(RF1662_SDA_PIN, PIN_LOW);
}


/*
********************************************************************************
* Function Name : RF1662_Start
* Description   : RF1662启动
* Parameter     : None
* Return        : None
********************************************************************************
*/
static void RF1662_Start(void)
{
    gpio_cfg_output(RF1662_SDA_PIN);
    
    gpio_pin_write(RF1662_SDA_PIN, PIN_LOW);
    delay_us(RF1662_DELAY_M);
    gpio_pin_write(RF1662_SDA_PIN, PIN_HIGH);
    delay_us(RF1662_DELAY_M);
    gpio_pin_write(RF1662_SDA_PIN, PIN_LOW);
    delay_us(RF1662_DELAY_M);
}


/*
********************************************************************************
* Function Name : RF1662_Stop
* Description   : RF1662停止
* Parameter     : None
* Return        : None
********************************************************************************
*/
static void RF1662_Stop(void)
{
    gpio_cfg_output(RF1662_SDA_PIN);
    
    gpio_pin_write(RF1662_SDA_PIN, PIN_LOW);
    gpio_pin_write(RF1662_SCK_PIN, PIN_HIGH);
    delay_us(RF1662_DELAY_N);
    gpio_pin_write(RF1662_SCK_PIN, PIN_LOW);
    delay_us(RF1662_DELAY_N);
}


/*
********************************************************************************
* Function Name : RF1662_Set_bit
* Description   : RF1662发送一个位
* Parameter     : None
* Return        : None
********************************************************************************
*/
static void RF1662_Set_bit(UINT8 bit_val)
{
    gpio_pin_write(RF1662_SCK_PIN, PIN_HIGH);
    
    if (bit_val)
    {
        gpio_pin_write(RF1662_SDA_PIN, PIN_HIGH);
    }
    else
    {
        gpio_pin_write(RF1662_SDA_PIN, PIN_LOW);
    }
    
    delay_us(RF1662_DELAY_N);
    gpio_pin_write(RF1662_SCK_PIN, PIN_LOW);
    delay_us(RF1662_DELAY_N);
}


/*
********************************************************************************
* Function Name : RF1662_Read_Register
* Description   : RF1662读寄存器
* Parameter     : - slave_addr: 从设备地址
*                 - reg_addr  : 寄存器地址
* Return        : None
********************************************************************************
*/
void RF1662_Read_Register(UINT8 slave_addr, UINT16 reg_addr)
{
    UINT8 i;
    UINT8 check_bit = 0;

    gpio_cfg_output(RF1662_SDA_PIN);

    // slave address
    for (i = 0; i < 4; i++)
    {
        if (slave_addr & 0x08)
        {
            check_bit++;
            RF1662_Set_bit(1);
        }
        else
        {
            RF1662_Set_bit(0);
        }
        slave_addr <<= 1;
    }

    // read flag
    RF1662_Set_bit(0);
    RF1662_Set_bit(1);
    check_bit++;
    RF1662_Set_bit(1);
    check_bit++;

    // register address
    for (i = 0; i < 5; i++)
    {
        if (reg_addr & 0x10)
        {
            check_bit++;
            RF1662_Set_bit(1);
        }
        else
        {
            RF1662_Set_bit(0);
        }
        reg_addr <<= 1;
    }

    // 校验位，奇校验
    if ((check_bit % 2) != 0)
    {
        RF1662_Set_bit(0);
    }
    else
    {
        RF1662_Set_bit(1);
    }
}


/*
********************************************************************************
* Function Name : RF1662_Read_Data
* Description   : RF1662读数据
* Parameter     : None
* Return        : 寄存器数据
********************************************************************************
*/
UINT8 RF1662_Read_Data(void)
{
    UINT8 i;
    UINT8 data = 0;

    gpio_cfg_input(RF1662_SDA_PIN, GPIO_PIN_CNF_PULL_Pullup);

    // read data
    for (i = 0; i < 8; i++)
    {
        gpio_pin_write(RF1662_SCK_PIN, PIN_HIGH);
        delay_us(RF1662_DELAY_N);
        
        data <<= 1;
        data |= (UINT8)gpio_pin_read(RF1662_SDA_PIN);
        
        gpio_pin_write(RF1662_SCK_PIN, PIN_LOW);
        delay_us(RF1662_DELAY_N);
    }
    
    // 读取校验位
    gpio_pin_write(RF1662_SCK_PIN, PIN_HIGH);  
    delay_us(RF1662_DELAY_N);
    gpio_pin_write(RF1662_SCK_PIN, PIN_LOW);
    delay_us(RF1662_DELAY_N);
 
    return data;
}


/*
********************************************************************************
* Function Name : RF1662_Write_Register
* Description   : RF1662寄存器
* Parameter     : - slave_addr: 从设备地址
*                 - reg_addr  : 寄存器地址
* Return        : None
********************************************************************************
*/
static void RF1662_Write_Register(UINT8 slave_addr, UINT16 reg_addr)
{
    UINT8 i;
    UINT8 check_bit = 0;
    
    gpio_cfg_output(RF1662_SDA_PIN);

    // slave address
    for (i = 0; i < 4; i++)
    {
        if (slave_addr & 0x08)
        {
            check_bit++;
            RF1662_Set_bit(1);
        }
        else
        {
            RF1662_Set_bit(0);
        }
        slave_addr<<=1;
    }

    // write flag
    RF1662_Set_bit(0);
    RF1662_Set_bit(1);
    check_bit++;
    RF1662_Set_bit(0);

    // register address
    for (i = 0; i < 5; i++)
    {
        if (reg_addr & 0x10)
        {
            check_bit++;
            RF1662_Set_bit(1);
        }
        else
        {
            RF1662_Set_bit(0);
        }
        reg_addr <<= 1;
    }
 
     // 校验位，奇校验
     if ((check_bit % 2) != 0)
     {
         RF1662_Set_bit(0);
     }
     else
     {
         RF1662_Set_bit(1);
     }
}


/*
********************************************************************************
* Function Name : RF1662_Write_Data
* Description   : RF1662写数据
* Parameter     : - data: 数据
* Return        : None
********************************************************************************
*/
static void RF1662_Write_Data(UINT8 data)
{
    UINT8 i;
    UINT8 check_bit = 0;

    gpio_cfg_output(RF1662_SDA_PIN);

    // write data
    for (i = 0; i < 8; i++)
    { 
        if (data & 0x80)
        {
            check_bit++;
            RF1662_Set_bit(1);
        }
        else
        {
           RF1662_Set_bit(0);
        }
        data <<= 1;
    }       

    // 校验位，奇校验
    if ((check_bit % 2) != 0)
    {
        RF1662_Set_bit(0);
    }
    else
    {
        RF1662_Set_bit(1);
    }
}


/*
********************************************************************************
* Function Name : RF1662_Write_Reg0
* Description   : 把数据写入写入寄存器0
* Parameter     : - slave_addr: 从设备地址
*                 - data      : 数据
* Return        : None
********************************************************************************
*/
static void RF1662_Write_Reg0(UINT8 slave_addr, UINT8 data)
{
    UINT8 i;
    UINT8 check_bit = 0;

    gpio_cfg_output(RF1662_SDA_PIN);

    // slave address
    for (i = 0; i < 4; i++)
    {
        if (slave_addr & 0x08)
        {
            check_bit++;
            RF1662_Set_bit(1);
        }
        else
        {
            RF1662_Set_bit(0);
        }
        slave_addr <<= 1;
    }

    // write flag
    RF1662_Set_bit(1);
    check_bit++;

    // write data
    for (i = 0; i < 7; i++)
    { 
        if (data & 0x40)
        {
            check_bit++;
            RF1662_Set_bit(1);
        }
        else
        {
           RF1662_Set_bit(0);
        }
        data <<= 1;
    }    

    // 校验位，奇校验
    if ((check_bit % 2) != 0)
    {
        RF1662_Set_bit(0);
    }
    else
    {
        RF1662_Set_bit(1);
    }
}


/*
********************************************************************************
* Function Name : RF1662_Select_ANT
* Description   : RF1662选择天线通道
* Parameter     : - ANT_TRX: 天线通道
* Return        : None
********************************************************************************
*/
void RF1662_Select_ANT(UINT8 ANT_TRX)
{
    RF1662_Start();
    RF1662_Write_Reg0(0x0A, ANT_TRX);
    RF1662_Stop();
}


/*
********************************************************************************
* Function Name : RF1662_Init
* Description   : RF1662初始化
* Parameter     : None
* Return        : None
********************************************************************************
*/
void RF1662_Init(void)
{
	
    RF1662_GPIO_Init();
    
    delay_us(20);
	
    RF1662_Start();
    RF1662_Write_Register(0x0A, PM_TRIG_Addr);
    RF1662_Write_Data(PM_TRIG_STARTUP_Data);  // 按照文档要求先配置为STARTUP模式      
    RF1662_Stop();
    
    delay_us(10);
    
    RF1662_Start();
    RF1662_Write_Register(0x0A, PM_TRIG_Addr);
    RF1662_Write_Data(PM_TRIG_ACTIVE_Data);  // 再配置为ACTIVE/LOW_POWER模式
    RF1662_Stop();
    
    RF1662_Start();
    RF1662_Write_Register(0x0A, PM_TRIG_Addr);
    RF1662_Write_Data(PM_TRIG_NO);           // 不需要触发, 直接写入设备(多个设备时才需要用到不同的触发)
    RF1662_Stop(); 
			 
    delay_us(1000); 

//		RF1662_Start();	 
//		RF1662_Read_Register(0x0A, PRODUCT_ID_Addr);	 
//		temp = RF1662_Read_Data();
//		temp = temp;
//		RF1662_Stop();
}



