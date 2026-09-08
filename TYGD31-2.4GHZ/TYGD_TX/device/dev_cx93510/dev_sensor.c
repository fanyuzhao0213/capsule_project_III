/*
****************************************************************************
* Copyright(C): TY Technology
* FileName    : dev_sensor.c
* Author      : TY Technical Software Development Team
* Description : 配置感光芯片
****************************************************************************
*/

#include "dev_sensor.h"
#include "dev_cx93510_regs.h"
#include "drv_spi.h"
#include "drv_gpio.h"



// SENSOR_OV7676 
#define SENSOR_DEV_ADDR         (0x78)    // 感光芯片OV7676设备地址
#define SENSOR_CTRL_INTERFACE   (0x00)    // 感光芯片控制接口 I2C
#define SENSOR_PARAM_ADDR_LEN   (0x02)    // 感光芯片参数地址长度
#define SENSOR_PARAM_DATA_LEN   (0x01)    // 感光芯片参数数据长度


// 感光芯片复位控制引脚
//#define SENSOR_PIN_SHDW              (11u)
#define SENSOR_PIN_SHDW              (6u)//(11u)



// 感光芯片配置数据结构体定义
typedef struct
{
    UINT8  *text;
    UINT16 saddr;
    UINT8  sddr_len;
    UINT8  data;
    UINT8  mdelay;
}SENSOR_DATA_t;

// 感光芯片OV7676配置数据
static const SENSOR_DATA_t sensor_data[] =
{
	{"",0x0103 , 2 ,0x01 , 0},
	{"",0x0001 , 2 ,0xAD , 0},
	{"",0x0002 , 2 ,0xAD , 0},
	{"",0x3001 , 2 ,0x07 , 0},
	{"",0x3002 , 2 ,0xFF , 0},

	{"",0x301E , 2 ,0x09 , 0},
	{"",0x3080 , 2 ,0x02 , 0},
	{"",0x3081 , 2 ,0x10 , 0},
	{"",0x3082 , 2 ,0x01 , 0},
	{"",0x3083 , 2 ,0x01 , 0},
	{"",0x3084 , 2 ,0x01 , 0},
	{"",0x3103 , 2 ,0x01 , 0},
	{"",0x3503 , 2 ,0x00 , 0},
	{"",0x3600 , 2 ,0x03 , 0},
	{"",0x3602 , 2 ,0x0E , 1},  //1MS



	{"",0x3612 , 2 ,0x0D , 0},
	{"",0x3614 , 2 ,0x53 , 0},
	{"",0x37C0 , 2 ,0x07 , 0},   
	{"",0x3800 , 2 ,0x00 , 0},
	{"",0x3801 , 2 ,0x50 , 0},//00
	{"",0x3802 , 2 ,0x00 , 0},
	{"",0x3803 , 2 ,0x00 , 0},
	{"",0x3804 , 2 ,0x02 , 0},
	{"",0x3805 , 2 ,0x37 , 0},
	{"",0x3806 , 2 ,0x01 , 0},
	{"",0x3807 , 2 ,0xE7 , 0},
	{"",0x3808 , 2 ,0x01 , 0}, // 分辨率
	{"",0x3809 , 2 ,0xE0 , 0},
	{"",0x380A , 2 ,0x01 , 0},
	{"",0x380B , 2 ,0xE0 , 1}, 

	{"",0x380C , 2 ,0x02 , 0},
	{"",0x380D , 2 ,0x87 , 0},
	{"",0x380E , 2 ,0x02 , 0},
	{"",0x380F , 2 ,0x32 , 0},
	{"",0x3810 , 2 ,0x00 , 0},
	{"",0x3811 , 2 ,0x04 , 0},
	{"",0x3812 , 2 ,0x00 , 0},
	{"",0x3813 , 2 ,0x04 , 0},
	
	{"",0x3820 , 2 ,0x10 , 0},
	{"",0x3821 , 2 ,0x00 , 0},

	{"", 0x3a00, 2, 0x51, 0},  //1??5060HZ??2?
	{"", 0x3a01, 2, 0x05, 0},  //蟀?礻?????
	{"", 0x3a02, 2, 0x7f, 0},

	{"",0x3A03 , 2 ,0x28 , 0},//0x58
	{"",0x3A04 , 2 ,0x26 , 0},//0x18
	{"",0x3A06 , 2 ,0x00 , 0},
	{"",0x3A07 , 2 ,0xA1 , 0},
	{"",0x3A08 , 2 ,0x00 , 0},
	{"",0x3A09 , 2 ,0x86 , 0},
	{"",0x3A0E , 2 ,0x01 , 1}, //1MS

	{"",0x3A0F , 2 ,0xE3 , 0},
	{"",0x3A10 , 2 ,0x01 , 0},
	{"",0x3A11 , 2 ,0xFC , 0},
	{"",0x4008 , 2 ,0x00 , 0},
	{"",0x4009 , 2 ,0x01 , 0},
	{"",0x4011 , 2 ,0xF0 , 0},
	{"",0x4013 , 2 ,0x05 , 0},
	{"",0x4014 , 2 ,0x05 , 0},
	{"",0x4015 , 2 ,0x05 , 0},
	{"",0x4017 , 2 ,0x08 , 0},
	{"",0x4300 , 2 ,0x03 , 0},
	{"",0x4301 , 2 ,0xFF , 0},
	{"",0x4304 , 2 ,0x03 , 0},
	{"",0x4305 , 2 ,0xFF , 0},
	{"",0x4308 , 2 ,0x04 , 1}, //1MS

	{"",0x4F00 , 2 ,0x80 , 0},
	{"",0x4F01 , 2 ,0x10 , 0},
	{"",0x4F02 , 2 ,0x00 , 0},
	{"",0x5000 , 2 ,0xfF , 0},
	{"",0x5001 , 2 ,0x3F , 0},
	{"",0x5002 , 2 ,0x48 , 0},

	{"",0x5007 , 2 ,0x2f , 0},

	{"",0x5080 , 2 ,0x00 , 0},

	{"",0x5200 , 2 ,0x14 , 0},
	{"",0x5201 , 2 ,0x02 , 0},
	{"",0x5202 , 2 ,0x02 , 0},
	{"",0x5203 , 2 ,0x53 , 0},

	{"",0x5204 , 2 ,0x03 , 0},
	{"",0x5205 , 2 ,0xBE , 0},
	{"",0x5206 , 2 ,0x00 , 0},
	{"",0x5207 , 2 ,0x7A , 0},
	{"",0x5208 , 2 ,0x03 , 0},
	{"",0x5209 , 2 ,0xE8 , 0},
	{"",0x520A , 2 ,0x00 , 0},
	{"",0x520B , 2 ,0x9A , 1}, //1MS

	{"",0x5210 , 2 ,0x02 , 0},
	{"",0x5211 , 2 ,0x02 , 0},

	//{"",0x5216 , 2 ,0x3a , 0},//

	//{"",0x521a , 2 ,0x0a , 0},
	//{"",0x521b , 2 ,0x00 , 0},
	//{"",0x521c , 2 ,0x07 , 0},
	//{"",0x521d , 2 ,0x00 , 0},
	//{"",0x521e , 2 ,0x0b , 0},
	//{"",0x521f , 2 ,0x00 , 0},

	{"",0x5228 , 2 ,0x03 , 0},
	{"",0x5229 , 2 ,0xE4 , 0},
	{"",0x522C , 2 ,0x00 , 0},
	{"",0x522D , 2 ,0x68 , 0},
	{"",0x522A , 2 ,0x00 , 0},
	{"",0x522B , 2 ,0x00 , 0},
	{"",0x522E , 2 ,0x00 , 0},
	{"",0x522F , 2 ,0x88 , 0},
	{"",0x5230 , 2 ,0x24 , 0},
	{"",0x550D , 2 ,0x00 , 0},
	{"",0x5500 , 2 ,0x10 , 0},
	{"",0x5501 , 2 ,0x24 , 0},
	{"",0x5502 , 2 ,0x22 , 1}, //1MS

	{"",0x5503 , 2 ,0x07 , 0},
	{"",0x5504 , 2 ,0x10 , 0},
	{"",0x5505 , 2 ,0x10 , 0},
	{"",0x5506 , 2 ,0x10 , 0},
	{"",0x5507 , 2 ,0x10 , 0},
	{"",0x5509 , 2 ,0x10 , 0},
	{"",0x550A , 2 ,0x24 , 0},

	{"",0x5600 , 2 ,0x48 , 0},
	//{"",0x5601 , 2 ,0x40 , 0},
	//{"",0x5602 , 2 ,0x06 , 0},
	//{"",0x5603 , 2 ,0x1a , 0},
	//{"",0x5604 , 2 ,0x3a , 0},
	//{"",0x5605 , 2 ,0x52 , 0},



	{"",0x5804 , 2 ,0x28 , 0},

	{"",0x5805 , 2 ,0x11 , 0},
	{"",0x5806 , 2 ,0x1d , 0},
	{"",0x5807 , 2 ,0x25 , 0},

	{"",0x2809 , 2 ,0x10 , 0},
	{"",0x580A , 2 ,0x40 , 0},
//	{"",0x0100 , 2 ,0x01 , 0},   
};



/*
********************************************************************************
* Function Name : sensor_write
* Description   : 感光芯片写数据
* Parameter     : None
* Return        : None
********************************************************************************
*/
static void sensor_write_data(UINT8 dev_addr, UINT16 sub_addr,  UINT8 sub_addr_len,
                              UINT8 *write_buf, UINT8 wirte_buf_len,
                              UINT8 en_sccb)
{
    UINT8 buffer;

    /* I2C route to FPIO4:3 */
    buffer = 0x00;
    spi_write_data(SPI0, I2C_CTL_3, &buffer,1);

    buffer = dev_addr;
    spi_write_data(SPI0, I2C_DADDR, &buffer,1);

    buffer = 0;
    if (sub_addr_len > 0) 
    {
        buffer |= FLD_I2C_READ_SA;
        buffer |= ((sub_addr_len << 6) & FLD_I2C_SADDR_LEN);
    }

    /* set SCCB enable or not */
    if (en_sccb)
    {
        buffer |= FLD_I2C_SCCB_EN;
    }
    spi_write_data(SPI0, I2C_CTL_2, &buffer,1);

    /* Write sub address Lower byte */
    buffer = (sub_addr & 0xff);
    spi_write_data(SPI0, I2C_LO_ADDR, &buffer,1);

    /* Write sub address Higher byte */
    if (sub_addr_len > 1) 
    {
        buffer = ((sub_addr >> 8) & 0xff);
        spi_write_data(SPI0, I2C_HI_ADDR, &buffer,1);
    }        

    buffer = 0x00;
    switch (wirte_buf_len) 
    {
        case 0x00:
            return;
        case 0x01:
            break;
        case 0x02:
            buffer |= FLD_I2C_DATA_16;
            break;
        default:
            break;
    }

    spi_write_data(SPI0, I2C_CTL_3, &buffer, 1);

    /* write the data */
    spi_write_data(SPI0, I2C_LO_DATA, &write_buf[0], 1);

    /* write if we have high byte */
    if(wirte_buf_len > 1)
    {
        spi_write_data(SPI0, I2C_HI_DATA, &write_buf[1], 1);
    }
}


/*
********************************************************************************
* Function Name : sensor_pin_init
* Description   : 感光芯片控制引脚复位
* Parameter     : None
* Return        : None
********************************************************************************
*/
static void sensor_pin_init(void)
{
  gpio_cfg_output(SENSOR_PIN_SHDW);
  
  gpio_pin_write(SENSOR_PIN_SHDW, PIN_LOW);
  delay_ms(5);
  
  gpio_pin_write(SENSOR_PIN_SHDW, PIN_HIGH);
  delay_ms(5);
}


/*
********************************************************************************
* Function Name : sensor_init
* Description   : 感光芯片初始化
* Parameter     : None
* Return        : None
********************************************************************************
*/
void sensor_init(void)
{
  UINT16 i;
  const SENSOR_DATA_t *p_sensor = sensor_data;

  sensor_pin_init();

  for(i = 0; i < (sizeof(sensor_data) / sizeof(SENSOR_DATA_t)); i++) 
  {       
      sensor_write_data(SENSOR_DEV_ADDR, p_sensor->saddr, p_sensor->sddr_len, (UINT8 *)&p_sensor->data, 1, SENSOR_CTRL_INTERFACE);

      if(p_sensor->mdelay)
      {
          delay_ms(p_sensor->mdelay);
      }
      
      delay_us(200);
      
      p_sensor++;
  }

  delay_ms(60);
}


/*
********************************************************************************
* Function Name : sensor_sleep
* Description   : 感光芯片进入休眠
* Parameter     : None
* Return        : None
********************************************************************************
*/
void sensor_sleep(void)
{
	UINT8 data = 0x00;
	sensor_write_data(SENSOR_DEV_ADDR, 0x0100, 2, &data, 1, 0);
}


/*
********************************************************************************
* Function Name : sensor_wakeup
* Description   : 唤醒感光芯片, 进入工作模式
* Parameter     : None
* Return        : None
********************************************************************************
*/
void sensor_wakeup(void)
{
	UINT8 data = 0x01;
	sensor_write_data(SENSOR_DEV_ADDR, 0x0100, 2, &data, 1, 0);
}

