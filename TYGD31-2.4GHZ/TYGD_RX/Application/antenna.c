/*
****************************************************************************
* Copyright(C): TY Technology
* FileName    : antenna.c
* Description : 天线处理
****************************************************************************
*/

#include "antenna.h"
#include "dev_rf1662.h"
#include "drv_systick.h"
#include "drv_radio.h"
#include "drv_timer.h"
#include "drv_device_id.h"
#include "capsule_sn_binding.h"
#include <string.h>



// 天线搜索时信号强度采样次数
#define ANTENNA_SCAN_TIMES                (36)

// 天线信号强度扫描周期，单位ms
#define ANTENNA_RSSI_SCAN_PERIOD          (3)

// 天线信号强度更新周期，单位ms
#define ANTENNA_RSSI_UPDATE_PERIOD        (50)

// 天线信号强度检测周期，单位ms
#define ANTENNA_RSSI_DETECT_PERIOD        (1200)



// 天线状态定义
typedef enum
{
    ANTENNA_STATUS_SCAN = 0,         // 天线搜索
    ANTENNA_STATUS_DETECT,           // 天线信号强度检测
} ANTENNA_STATUS_t;



// 天线扫描过程中的天线号
static UINT8 antenna_scan_id = 0;

// 天线扫描过程中的信号强度
static UINT8 antenna_scan_rssi[ANTENNA_AMT_MAX] = {0};

// 天线检测过程中的天线号
static UINT8 antenna_detect_id = 0;

// 天线检测过程中的信号强度
static UINT8 antenna_detect_rssi[ANTENNA_AMT_MAX] = {0};

// 天线状态
static ANTENNA_STATUS_t antenna_status = ANTENNA_STATUS_SCAN;

// 天线信号强度检测计数，每接收到一张图像，数值加1
UINT8 antenna_detect_cnt = 0;

// 天线检测时间
static UINT32 antenna_detect_time = 0;



/*
********************************************************************************
* Function Name : antenna_read_info
* Description   : 读取天线相关信息到指定buffer
* Parameter     : - buff: 拷贝天线相关信息的buffer
* Return        : None
********************************************************************************
*/
void antenna_read_info(UINT8 *buff)
{
    // 检查入参
    if (NULL == buff)
    {
        return;
    }

    // 拷贝天线号
    buff[49] = antenna_detect_id+1;

    // 无线频段
    buff[50] = NRF_RADIO->FREQUENCY;
    
    // 拷贝天线信号强度
    buff[39] = antenna_detect_rssi[0x00];
    buff[40] = antenna_detect_rssi[0x01];
    buff[41] = antenna_detect_rssi[0x02];
    buff[42] = antenna_detect_rssi[0x03];
    buff[43] = antenna_detect_rssi[0x04];
    buff[44] = antenna_detect_rssi[0x05];
    buff[51] = antenna_detect_rssi[0x06];
    buff[52] = antenna_detect_rssi[0x07];
    buff[53] = antenna_detect_rssi[0x08];
    buff[54] = antenna_detect_rssi[0x09];
    buff[55] = antenna_detect_rssi[0x0A];
    buff[56] = antenna_detect_rssi[0x0B];
    buff[57] = antenna_detect_rssi[0x0C];
    buff[58] = antenna_detect_rssi[0x0D];
    buff[59] = antenna_detect_rssi[0x0E];
    buff[60] = antenna_detect_rssi[0x0F];
}


/*
********************************************************************************
* Function Name : antenna_change_to_scan
* Description   : 切换到天线搜索状态
* Parameter     : None
* Return        : None
********************************************************************************
*/
void antenna_change_to_scan(void)
{
    antenna_change(antenna_scan_id);
    antenna_status = ANTENNA_STATUS_SCAN;
    memset(antenna_scan_rssi, 0, sizeof(antenna_scan_rssi));
    timer1_set_period(ANTENNA_RSSI_SCAN_PERIOD);
}


/*
********************************************************************************
* Function Name : antenna_change_to_detect
* Description   : 切换到天线(信号强度)扫描状态
* Parameter     : None
* Return        : None
********************************************************************************
*/
static void antenna_change_to_detect(void)
{
    antenna_status = ANTENNA_STATUS_DETECT;
    antenna_detect_cnt = 0;
    antenna_detect_time = 0;
    memcpy(antenna_detect_rssi, antenna_scan_rssi, ANTENNA_AMT_MAX);
    timer1_set_period(ANTENNA_RSSI_UPDATE_PERIOD);
}


/*
********************************************************************************
* Function Name : antenna_scan
* Description   : 天线搜索，搜索出信号强度最强的天线
* Parameter     : None
* Return        : None
********************************************************************************
*/
static void antenna_scan(void)
{
    static UINT16 count = 0;
    UINT8 i, id, rssi;

    // 记录当前天线最强的信号值
    if (radio_rssi_value > 0)
    {
        if (antenna_scan_rssi[antenna_scan_id] == 0 || radio_rssi_value < antenna_scan_rssi[antenna_scan_id])
        {
            antenna_scan_rssi[antenna_scan_id] = radio_rssi_value;
        }
        
        count++;
    }

    // 达到设定的采样次数后，搜索信号强度最强的天线
    if (count >= ANTENNA_SCAN_TIMES)
    {
        // 搜索信号强度最强的天线
        for (i = 0, id = 0, rssi = 0xFF; i < ANTENNA_AMT_USED; i++)
        {
            if (antenna_scan_rssi[i] > 0 && antenna_scan_rssi[i] < rssi)
            {
                rssi = antenna_scan_rssi[i];
                id = i;
            }
        }

        // 搜索完毕，切换天线
        antenna_detect_id = id;
        antenna_scan_id = id;
        antenna_change_to_detect();
        count = 0;
    }
    else
    {
        antenna_scan_id = (antenna_scan_id + 1) % ANTENNA_AMT_USED; // 切换下一个天线
    }

    // 天线设置生效
    antenna_change(antenna_scan_id);
}


/*
********************************************************************************
* Function Name : antenna_detect_rssi
* Description   : 天线(信号强度)检测
* Parameter     : None
* Return        : None
********************************************************************************
*/
static void antenna_detect(void)
{
    static UINT8  rssi = 0;

    // 初始化检测时间
    if (antenna_detect_time == 0)
    {
        antenna_detect_time = systick_get_time();
    }

    // 记录当前天线最强的信号强度值
    if (rssi == 0 || (radio_rssi_value > 0 && radio_rssi_value < rssi))
    {
        rssi = radio_rssi_value;
    }

    // 如果在(ANTENNA_RSSI_DETECT_PERIOD)的时间内接收不到一张完整的图像，则重新开始搜索天线
    if (systick_get_time() - antenna_detect_time >= ANTENNA_RSSI_DETECT_PERIOD)
    {
        if (antenna_detect_cnt == 0)
        {
            antenna_change_to_scan();
        }
        else
        {
            antenna_detect_rssi[antenna_detect_id] = rssi;
            antenna_detect_cnt = 0;
        }

        // 参数复位
        rssi = 0;
        antenna_detect_time = 0;
    }
}


/*
********************************************************************************
* Function Name : antenna_change
* Description   : 切换天线, 使用天线号对应的天线通道接收射频信号
* Parameter     : ant_num: 天线号
* Return        : None
********************************************************************************
*/
void antenna_change(UINT8 ant_num)
{
    switch (ant_num)
    {
        case 0:
            RF1662_Select_ANT(ANT_TRX1);
            break;
        case 1:
            RF1662_Select_ANT(ANT_TRX2);
            break;
        case 2:
            RF1662_Select_ANT(ANT_TRX3);
            break;
        case 3:
            RF1662_Select_ANT(ANT_TRX4);
            break;
        case 4:
            RF1662_Select_ANT(ANT_TRX5);
            break;
        case 5:
            RF1662_Select_ANT(ANT_TRX6);
            break;
        case 6:
            RF1662_Select_ANT(ANT_TRX7);
            break;
        case 7:
            RF1662_Select_ANT(ANT_TRX8);
            break;
        case 8:
            RF1662_Select_ANT(ANT_TRX9);
            break;
        case 9:
            RF1662_Select_ANT(ANT_TRX10);
            break;
        case 10:
            RF1662_Select_ANT(ANT_TRX11);
            break;
        case 11:
            RF1662_Select_ANT(ANT_TRX12);
            break;
        default:
            break;
    }
}


/*
********************************************************************************
* Function Name : antenna_init
* Description   : 天线初始化
* Parameter     : None
* Return        : None
********************************************************************************
*/
void antenna_init(void)
{
    // 初始化RF1662
    RF1662_Init();

    // 设置默认天线号
    antenna_detect_id = 0;
    antenna_scan_id = 0;
    antenna_change(antenna_scan_id);
    
    // 设置默认天线状态
    antenna_status = ANTENNA_STATUS_SCAN;
	
    // 设置天线处理回调函数
    timer1_set_cbk_func(antenna_process);

    // 设置天线扫描周期
    timer1_set_period(ANTENNA_RSSI_SCAN_PERIOD);
}


/*
********************************************************************************
* Function Name : antenna_process
* Description   : 天线处理函数
* Parameter     : None
* Return        : None
********************************************************************************
*/
void antenna_process(void)
{
    if (antenna_status == ANTENNA_STATUS_DETECT)
    {
        antenna_detect();
    }
    else
    {
        antenna_scan();
    }

    radio_rssi_value = 0;
}


