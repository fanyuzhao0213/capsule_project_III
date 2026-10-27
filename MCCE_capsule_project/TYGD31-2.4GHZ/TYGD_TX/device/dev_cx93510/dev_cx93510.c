/*
****************************************************************************
* Copyright(C): TY Technology
* FileName    : dev_cx93510.c
* Author      : TY Technical Software Development Team
* Description : 配置cx93510
****************************************************************************
*/

#include "dev_cx93510_regs.h"
#include "dev_cx93510.h"
#include "dev_sensor.h"
#include "dev_led.h"
#include "drv_spi.h"
#include "drv_gpio.h"
#include "drv_timer.h"
#include "drv_wdt.h"



// cx93510电源控制引脚
//#define CX93510_PIN_PDRAM              (21u)
#define CX93510_PIN_PDRAM              (18u)//(21u)
//#define CX93510_PIN_PD                 (3u)

// 图像压缩率宏定义
#define IMG_COMPRESSIBILITY_LOW     (0)      // 低
#define IMG_COMPRESSIBILITY_HIGH    (1)      // 高

// 一帧图像数据最小字节数
#define FRAME_DATA_MIN              (1024)

// 拍摄每帧图像所需要的场数
#define THE_NUM_OF_VSYNC_PER_FRAME  (2)

// 每个完整场信号的时间, 单位ms
#define THE_TIME_PER_VSYNC          (25)

BOOL FRAME_DATA_SIZE_GT_20KB_FLAGE = FALSE ;//帧图像数据大于20KB标志



// 图像压缩率
static UINT8 img_compressibility = IMG_COMPRESSIBILITY_LOW;



/*
********************************************************************************
* Function Name : cx93510_wait_for_done
* Description   : 等待配置完成
* Parameter     : None
* Return        : None
********************************************************************************
*/
static void cx93510_wait_for_done(void)
{
    UINT8 buffer;
    UINT8 i;
 
    /* wait for configuration done */
    for (i = 0; i < 10; i++) 
    {
        buffer = 0x0;
        spi_read_data(SPI0, JPEG_DEC_STAT3, &buffer, 1);

        if ((buffer & 0x01) == 0x01)
        {
            return;
        }
        
        delay_us(1);
    }
}


/*
********************************************************************************
* Function Name : cx93510_init_pin
* Description   : cx93510控制引脚初始化
* Parameter     : None
* Return        : None
********************************************************************************
*/
void cx93510_init_pin(void)
{
//    gpio_cfg_output(CX93510_PIN_PD);
		gpio_cfg_output(CX93510_PIN_PDRAM);
    
//    gpio_pin_write(CX93510_PIN_PD, PIN_LOW);
		gpio_pin_write(CX93510_PIN_PDRAM, PIN_LOW);
    delay_ms(10);
}


/*
********************************************************************************
* Function Name : cx93510_start
* Description   : cx93510启动
* Parameter     : None
* Return        : TRUE: 启动成功    FALSE: 启动失败
********************************************************************************
*/
BOOL cx93510_start(void)
{
    UINT8 buffer = 0;
    UINT8 count = 0;

//    gpio_pin_write(CX93510_PIN_PD, PIN_HIGH);
		gpio_pin_write(CX93510_PIN_PDRAM, PIN_HIGH);
	
    delay_ms(5);

    while (1)
    {
        buffer = 0;
        spi_read_data(SPI0, SLAVE_SEL_CTRL, &buffer, 1);

        if (buffer == 0x01)
        {
            return TRUE;
        }
        else if (++count >= 10)
        {
            return FALSE;
        }
        else
        {
            delay_ms(1);
        }
    }
}


/*
********************************************************************************
* Function Name : cx93510_close
* Description   : cx93510关闭
* Parameter     : None
* Return        : None
********************************************************************************
*/
void cx93510_close(void)
{
//   gpio_pin_write(CX93510_PIN_PD, PIN_LOW);
	 gpio_pin_write(CX93510_PIN_PDRAM, PIN_LOW);
}


/*
********************************************************************************
* Function Name : cx93510_init
* Description   : cx93510初始化
* Parameter     : None
* Return        : None
********************************************************************************
*/
void cx93510_init(void)
{   
	static UINT8 res_count = 0;
	
ReRun:  
    
    cx93510_init_pin();

    if (cx93510_start() == FALSE) 
    {
        spi_master_reset(SPI0);
        spi_master_init(SPI0);
        spi_master_reset(SPI0);
        spi_master_init(SPI0);
			
				// 如果多次93510初始化失败，软件复位
				res_count++;
				if (res_count >= 10)
				{
					res_count = 0;
					NVIC_SystemReset();
				}
				
				// 喂狗
				wdt_feed();                  
				
        goto ReRun;
    }

    sensor_init();
    delay_ms(10);

    cx93510_close();
    delay_ms(10);
}


/*
********************************************************************************
* Function Name : cx93510_setup
* Description   : cx93510设置参数
* Parameter     : None
* Return        : None
********************************************************************************
*/
void cx93510_setup(void)
{   
    UINT8 buffer = 0;
    
    /* reset Jpeg encoder */
    buffer = FLD_RST_COMP;
    spi_write_data(SPI0, DIFF_JPEG_CTRL, &buffer, 1);
    cx93510_wait_for_done();
    
    buffer = 0;
    spi_write_data(SPI0, DIFF_JPEG_CTRL, &buffer, 1);

    if (img_compressibility == IMG_COMPRESSIBILITY_LOW)
    {
        buffer = 0x00;  // 0x02 高   // 0x00低 
        spi_write_data(SPI0, JPEG_ENC_DCT_LM, &buffer, 1); // 压缩率设置
        buffer = 0x01;  // 0x03 高   // 0x01 低
        spi_write_data(SPI0, JPEG_ENC_DCT_CH, &buffer, 1); // 压缩率设置
    }
    else
    {
        buffer = 0x02;  // 0x02 高   // 0x00低 
        spi_write_data(SPI0, JPEG_ENC_DCT_LM, &buffer, 1); // 压缩率设置
        buffer = 0x03;  // 0x03 高   // 0x01 低
        spi_write_data(SPI0, JPEG_ENC_DCT_CH, &buffer, 1); // 压缩率设置
    }
    
    buffer = 0x24;
    spi_write_data(SPI0, SI_CFG_1, &buffer, 1);
    
    buffer = 0x01;
    spi_write_data(SPI0, SI_CFG_3, &buffer, 1); // 设置取多少幅图片
    
    buffer = 0;
    spi_write_data(SPI0, H_ACT, &buffer, 1);
    
    buffer = 60;   // 取图的分辨率 *8
    spi_write_data(SPI0, H_CAP_WIDTH, &buffer, 1);
    spi_write_data(SPI0, V_CAP_HEIGHT, &buffer, 1);

    buffer = 0x00; // 取图的起点 *8
    spi_write_data(SPI0, H_CAP_DLY, &buffer, 1);
    
    buffer = 0x00; // 取图的起点
    spi_write_data(SPI0, V_CAP_DLY, &buffer, 1);
        
    // reload jpeg
    spi_read_data(SPI0, JPEG_ENC_CTL, &buffer, 1);
    buffer &= 0xFE;
    spi_write_data(SPI0, JPEG_ENC_CTL, &buffer, 1);

    // 打开闪光灯
    led_set(ON);
    
    // 唤醒感光芯片
    sensor_wakeup();
    
    cx93510_wait_for_done();
    delay_ms((THE_NUM_OF_VSYNC_PER_FRAME - 1) * THE_TIME_PER_VSYNC);

    buffer = 0x80;
    spi_write_data(SPI0, SI_CFG_2, &buffer, 1);
}


/*
********************************************************************************
* Function Name : cx93510_get_image
* Description   : cx93510获取图像
* Parameter     : - image_buf: 图像buffer
* Return        : 图像字节长度
********************************************************************************
*/
UINT16 cx93510_get_image(UINT8 *image_buf)
{
    UINT8  i, buffer = 0;
    UINT8  status_qword[8] = {0};
    UINT16 frm_len;
    
    buffer = 0;
    spi_write_data(SPI0, FB_ADDR_0, &buffer, 1);

    buffer = 0;           
    spi_write_data(SPI0, FB_ADDR_1, &buffer, 1);

    /* video mode & lower 3 bits of address */
    buffer = 0x10;           
    spi_write_data(SPI0, FB_ADDR_2, &buffer, 1);      

    /* read the prefetch loop until the prefetch is done */
    buffer = 0x0;     
    for (i = 0; i < 10; i++ )
    {
        spi_read_data(SPI0, FB_ADDR_2, &buffer, 1); 

        if(buffer & 0x20)
        {
            break;
        }
    }
    
    /* read the first 8 bytes */
    spi_read_data(SPI0, HRDATA_FB, status_qword, 8);

    // 计算帧长度
    frm_len = (UINT16)((status_qword[6] << 8) + status_qword[7]);
    if (frm_len % 8)
    {
        frm_len += 8 - (frm_len % 8);
    }

    if(status_qword[0] != 0x10 || frm_len < FRAME_DATA_MIN)  // 无图
    {
        return 0;
    }
    else if (frm_len > FRAME_DATA_SIZE)                      // 图像过大
    {
        img_compressibility = IMG_COMPRESSIBILITY_HIGH;
			  FRAME_DATA_SIZE_GT_20KB_FLAGE = TRUE;                //帧图像数据大于20KB标志置位
        return 0;
    }
    else if (frm_len < (FRAME_DATA_SIZE / 2))                // 图像过小
    {
        img_compressibility = IMG_COMPRESSIBILITY_LOW;
    }
    else
    {
        // Do nothing.
    }

    // 感光芯片进入休眠
    sensor_sleep();
    led_set(OFF);

    // 读取图像数据
    spi_read_data(SPI0, HRDATA_FB, image_buf, frm_len);
    
    return frm_len;
}


