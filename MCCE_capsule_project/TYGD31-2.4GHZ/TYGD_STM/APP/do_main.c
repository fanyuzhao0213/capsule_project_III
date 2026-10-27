/*
****************************************************************************
* Copyright(C): TY Technical
* FileName    : do_main.c
* Author      : TY Technical Software Development Team
* Description : 应用层主循环
*               ├─ 底层驱动初始化（电源 / SD 卡 / 看门狗）
*               ├─ 应用层初始化（UART / JPEG 缓冲 / 设备信息）
*               └─ while(1) 主循环：图像接收 → SD 存储 → UART3 转发 → 序列号绑定
*
* 数据流（单帧生命周期）：
*   RF 接收板 ──UART3──> 本 STM32 ──> SD 卡（FATFS 追加）
*                              └─> UART3 ──> PC（实时显示）
*                              └─> 蜂鸣器（信号弱报警，原工程预留）
****************************************************************************
*/
#include "do_main.h"
// 驱动层头文件
#include "drv_power_mode.h"        // 电源模式（上下电、GPIO 配置）
#include "drv_usart.h"             // UART1/2/3 驱动（DMA + 中断）
#include "drv_wdt.h"               // 独立看门狗（IWDG）
// 应用层头文件
#include "jpeg_head.h"             // JPEG 数据缓冲定义（Data_Buf0）
#include "time_handle.h"           // RTC SD2058 时间 + 时间戳
#include "system_stm32_tick.h"      // 系统滴答 API
#include "receive_send_image.h"     // 图像接收 / UART3 发送逻辑
#include "storage_card.h"           // SD 卡 FATFS 存储
#include "delay.h"                  // 毫秒级延时
#include "usart_transfer_station.h" // UART2 中转站（PC <-> RF 板 SN 命令）
#include <string.h>
#include "calendar.h"              // RTC 时间结构体
#include "drv_sd2058.h"            // SD2058 RTC 芯片驱动
#include "exti.h"                  // 外部中断（SD2058 频率中断）
#include "bsp_TiMbase.h"           // 定时器基础（信号强度报警）


// 蜂鸣器响持续时间（信号弱或丢包时报警）
#define beep_on_delay_ms  10
// 蜂鸣器周期（每次响起的间隔）
#define beep_on_cycly_ms  3000

// 文件创建标志：当前是否已打开以胶囊 SN 命名的 .YS 文件
BOOL Capsule_File_FLAG = FALSE;



/*
********************************************************************************
* Function Name : driving_program_Init
* Description   : 底层驱动初始化（硬件相关）
* Paramter      : None
* Return        : None
*
* 初始化顺序与原因：
*   ① Power_Mode_GPIOConfig + power_up()
*        - 电源相关 GPIO 配置 + 上电动作
*        - 必须在其他外设之前，确保系统有稳定电源
*
*   ② switch_on_uptime_init()
*        - 更新时间初始化（与 RTC 同步）
*
*   ③ SD_Card_Open_File()
*        - 挂载 FATFS + 打开/创建默认文件
*        - 必须在中断开启之前完成（SD 卡初始化耗时较长，会阻塞中断）
*
*   ④ Iwdg_init()
*        - 启动独立看门狗（IWDG）
*        - 必须在 SD 卡初始化之后、看门狗启动之前完成
*        - 如果 SD 卡初始化卡死，看门狗能复位系统
********************************************************************************
*/
static void driving_program_Init(void)
{
  // 电源 GPIO 配置
  Power_Mode_GPIOConfig();

  // 执行上电动作
  power_up();

//  //初始化蜂鸣器（原工程保留，已注释）
//  Beep_Init();

  // 更新时间初始化（与 RTC 同步）
  switch_on_uptime_init();

//  //外部中断初始化（SD2058 频率中断，原工程保留，已注释）
//  EXTI_GPIO_Init();

  // SD 卡挂载 + 默认文件打开（耗时操作，必须在中断开启前）
  SD_Card_Open_File();

  // 启动看门狗（IWDG，独立看门狗，4kHz LSI 时钟）
  // 注意：看门狗必须在 SD 卡初始化之后启动，否则 SD 卡异常会导致永久死机
  Iwdg_init();

//  //初始化定时器（信号强度报警蜂鸣器时间控制，原工程保留，已注释）
//  TIMx_Configuration();

  return ;
}



/*
********************************************************************************
* Function Name : app_program_Init
* Description   : 应用层初始化（业务相关）
* Paramter      : None
* Return        : None
*
* 初始化内容：
*   ① USART3_Configuration()
*        - UART3 配置：1Mbps 8N1
*        - TX=P0.15（向 STM32 转发 → PC） / RX=P0.16（接收 RF 板 → STM32）
*        - 实际接 RF 接收板，接收完整图像帧
*
*   ② USART2_Configuration()
*        - UART2 配置：1Mbps 8N1
*        - 用于 PC 与 RF 板之间的 SN 绑定命令中转
*
*   ③ jpeg_data_buf_Init()
*        - 初始化 Data_Buf0[BUFF_SIZE]：
*            [0..127]      预头部（zeros + 标记）
*            [128..813]    JPEG 标准头（FF D8 + APP0 + DQT + DHT + SOF + SOS）
*        - 这是图像接收的"模板"，后续 JPEG 数据填充到 [814..]
*
*   ④ memset(DeviceInfo, 0, 128)
*        - 清零设备信息缓冲（128 字节）
*        - 后续由 receive_image_data_end / device_info_rewrite 填充
********************************************************************************
*/
static void app_program_Init(void)
{
//  // 新格式文件头写入（首次上电时一次性写 200640 字节头）
//  // 注：原工程注释保留，文件头会在 SD_Card_Open_Capsule_File 内部按需写入
//  SD_Card_write_file_header();

  // UART3 配置（接收 RF 板数据 + 转发给 PC）
  USART3_Configuration();

  // UART2 配置（PC 与 RF 板的 SN 绑定命令中转）
  USART2_Configuration();

  // JPEG 数据缓冲初始化（模板：预头部 + JPEG header）
  jpeg_data_buf_Init();

  // 清零设备信息（128 字节）
  memset(DeviceInfo, 0, 128);

  return ;
}



/*
********************************************************************************
* Function Name : domain
* Description   : 应用层主循环（永不返回）
* Paramter      : None
* Return        : None
*
* 主循环流程（每轮）：
*   ① Iwdt_FeedDog()           ── 喂狗，防止 IWDG 复位
*   ② MCU_uptime_now()          ── 更新 MCU 时间戳
*   ③ Receive_Image_Data_Bef_Sta=End_Sta  ── 保存上一轮状态（用于丢包检测）
*   ④ if (receive_image_data_end())        ── 是否收到完整图像？
*        ├─ 第一次收到：SD_Card_Open_Capsule_File(&DeviceInfo[11])
*        │                   以胶囊 SN 命名创建 .YS 文件
*        ├─ Read_Rtc_From_SD_Card()        ── 从 SD 卡读取 RTC 上次更新时间
*        ├─ SD2058_ReadBytes_From_Begin()    ── 从 RTC 读取当前时间
*        ├─ SD_Card_storage_imgdata()        ── 写入 SD 卡（追加一帧）
*        ├─ send_image_to_PC(CMD_IMG_FORWARD) ── UART3 转发给 PC
*        └─ SD_Card_storage_10_flush()        ── 每 10 帧 fsync 一次（性能优化）
*
*   ⑤ capsule_sn_CMDDeal()      ── 处理 PC ↔ RF 板的 SN 绑定命令中转
*
*   ⑥ 报警逻辑（已注释）：RSSI 弱 / 数据丢失蜂鸣器报警
*
* 关键设计：
*   - 喂狗放在循环开头：保证每轮循环最多 ~数 ms 内喂一次
*   - 图像接收是非阻塞的：UART3 中断持续接收，receive_image_data_end() 仅检查标志
*   - SD 卡写盘是阻塞的：f_write / f_sync 会阻塞数 ms
*   - 报警逻辑全部注释：原工程预留但当前不启用
********************************************************************************
*/
void domain(void)
{
  // 蜂鸣器持续响时间标志（信号弱报警用，原工程已禁用）
  u32 beep_time_flag = 0;
  // 蜂鸣器初始时间（丢包报警用）
  u32 beep_on_data_lost_time_init = 0;

  u32 beep_on_rssi_low_time_init = 0;

//  //上次最大RSSI值（信号质量统计，原工程已禁用）
//  u8 Rssi_This_Max_Data;
//  u8 Rssi_Last_Max_Data = 75;

  // 最大RSSI值低于阈值次数（信号弱报警计数器，原工程已禁用）
  u8 CH_REC_SIGNAL_STRENGTH_LOW_TIME = 0;

  // 最大RSSI值低于阈值标志
  BOOL CH_REC_SIGNAL_STRENGTH_LOW_FLAGE = FALSE;

  // 蜂鸣器报警标志
  BOOL BEEP_ON_ALARM_FLAG = FALSE;

  // 数据丢失状态（用于检测连续 N 轮没收到图像 = 丢包）
  u32 Receive_Image_Data_Bef_Sta = 0;
  u32 Receive_Image_Data_End_Sta = 0;

//  //胶囊数据连接成功标志（原工程已禁用）
//  BOOL CAPCULE_DATA_FIRST_CONNECT_FLAG = FALSE;

  // 底层驱动初始化（电源 / SD / 看门狗）
  driving_program_Init();

  // 应用层初始化（UART / JPEG 缓冲 / 设备信息）
  app_program_Init();

  // ──────────── 主循环（永不返回） ────────────
  while(1)
  {
    // ① 喂狗：每轮循环必须喂一次 IWDG，否则 ~1.6s 后系统复位
    Iwdt_FeedDog();

    // ② 更新 MCU 时间戳（基于 SysTick）
    MCU_uptime_now();

//  //根据PC时间设置RTC时间（同步 PC 时钟到 RTC）
//  PC_cmd_bcd_uptime();

//  //RTC时间更新
//  RTC_uptime_now();

    // ③ 保存上一轮的"图像接收完成"状态（用于丢包检测）
    Receive_Image_Data_Bef_Sta = Receive_Image_Data_End_Sta;

    // ④ 是否收到完整图像帧？（非阻塞查询 Img_data_reday 标志）
    if (TRUE == receive_image_data_end())
    {
      // 图像接收期间不响应串口2命令（避免数据被串口2污染）
      Img_process_finsh = 1;

      // 第一次收到图像：创建以胶囊 SN 命名的 .YS 文件
      if(Capsule_File_FLAG == FALSE)
      {
        Capsule_File_FLAG = TRUE;
        // DeviceInfo[11..18] 是胶囊 SN 字段（8 字节）
        SD_Card_Open_Capsule_File(&DeviceInfo[11]);

        // 从 SD 卡读取 RTC 上次更新时间（断电保持）
        Read_Rtc_From_SD_Card();
      }

      // 从 RTC 芯片读取当前时间（BCD 格式）
      SD2058_ReadBytes_From_Begin((u8*)&TimeNow, 7);  // 7 字节：年/月/日/周/时/分/秒

      Img_process_finsh = 0;

      // ⑤ SD 卡存储：把当前图像帧追加到 .YS 文件
      SD_Card_storage_imgdata();

      // ⑥ UART3 转发：把图像通过 UART3 发给 PC（实时显示）
      send_image_to_PC(CMD_IMG_FORWARD);

      // ⑦ 每 10 帧 fsync 一次（性能优化：避免每帧都 sync）
      SD_Card_storage_10_flush();

//      // 通道信号弱报警（原工程已禁用）
//      if(Rssi_Max_Data(DeviceInfo)>CH_REC_SIGNAL_STRENGTH_LOW_TH)
//      {
//        CH_REC_SIGNAL_STRENGTH_LOW_TIME ++;
//        if(CH_REC_SIGNAL_STRENGTH_LOW_TIME>CH_REC_SIGNAL_STRENGTH_ALA_TH)
//        {
//          CH_REC_SIGNAL_STRENGTH_LOW_TIME = 0;
//          CH_REC_SIGNAL_STRENGTH_LOW_FLAGE = TRUE;
//        }
//      }
//      else
//      {
//        CH_REC_SIGNAL_STRENGTH_LOW_TIME = 0;
//        CH_REC_SIGNAL_STRENGTH_LOW_FLAGE = FALSE;
//      }

//      //数据丢失标志
//      Receive_Image_Data_End_Sta++;

//      beep_on_data_lost_time_init=get_sys_tick_time();
    }

    // ⑤ 序列号绑定中转：处理 PC ↔ RF 板的 SN 命令
    capsule_sn_CMDDeal();

//////查询报警模式（已禁用）
//    if((Receive_Image_Data_Bef_Sta==Receive_Image_Data_End_Sta)&&(Receive_Image_Data_Bef_Sta))// 数据丢失报警
//    {
//      if((get_sys_tick_time()-beep_on_data_lost_time_init)>beep_on_cycly_ms)
//      {
//        beep_on_data_lost_time_init = get_sys_tick_time();
//        // 数据丢失报警
//        Beep_On();
//        beep_time_flag = get_sys_tick_time();
//        BEEP_ON_ALARM_FLAG = TRUE;
//      }
//    }
//    else
//    {
//      if(CH_REC_SIGNAL_STRENGTH_LOW_FLAGE)// 通道信号弱报警
//      {
//        if((get_sys_tick_time()-beep_on_rssi_low_time_init)>beep_on_cycly_ms)
//        {
//          beep_on_rssi_low_time_init = get_sys_tick_time();
//          // 通道信号弱报警
//          Beep_On();
//          beep_time_flag = get_sys_tick_time();
//          BEEP_ON_ALARM_FLAG = TRUE;
//        }
//      }
//    }

//  // 关闭蜂鸣器
//  if(BEEP_ON_ALARM_FLAG)
//  {
//    if((get_sys_tick_time()- beep_time_flag)>beep_on_delay_ms)
//    {
//      Beep_Off();
//      beep_time_flag = 0;
//      BEEP_ON_ALARM_FLAG = FALSE;
//    }
//  }

  }  // while(1) 主循环结束
}
