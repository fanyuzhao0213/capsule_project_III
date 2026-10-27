/*
****************************************************************************
* Copyright(C): TY Technical
* FileName    : storage_card.c
* Author      : TY Technical Software Development Team
* Description : SD 卡 FATFS 存储处理
*
* 文件结构：
*   - 每个胶囊一个文件：0:Y<SN>.YS
*   - 文件起始：200640 字节文件头（FILE_HEADER_SIZE）
*   - 每帧：20064 字节（FRAME_SIZE = 128 DevInfo + 686 JPEG header + 19250 JPEG data）
*
* 容量策略：
*   - 文件大小上限 0x75B81BC0（≈1.97 GB）
*   - 剩余空间 < 5GB 报警（蜂鸣器）
*
* RTC 持久化：
*   - 地址 83（文件内偏移）写入 7 字节 RTC 时间
*   - 下次开机从此处读取 RTC 时间（断电保持）
****************************************************************************
*/
#include "storage_card.h"
#include "jpeg_head.h"
#include "time_handle.h"
#include "receive_send_image.h"
#include "drv_wdt.h"
#include "system_stm32_tick.h"
#include "stm3210e_eval_sdio_sd.h"
#include <stdio.h>
#include <string.h>
#include "calendar.h"
#include "drv_usart.h"
#include "drv_power_mode.h"

//#define fre_sect_num 14680064ul // TF 卡剩余 7GB 对应扇区数
//#define fre_sect_num 2097152ul  // TF 卡剩余 1GB 对应扇区数
#define fre_sect_num 10485760ul   // TF 卡剩余 5GB 对应扇区数（报警阈值）

// 蜂鸣器响持续时间
#define tf_alarm_beep_on_delay_ms  10
// 蜂鸣器报警周期
#define tf_alarm_beep_on_cycly_ms   2000

// RTC 上次更新时间在文件内的偏移地址
#define RTC_Uptime_Adr  83

// 存放 RTC 更新时间的 7 字节缓冲（年/月/日/周/时/分/秒，BCD）
u8 Rtc_Uptime_buf[7] = {0};


// FATFS 文件系统返回值
FRESULT Sd_fr;
// FATFS 文件系统对象
FATFS Sd_fs;
// 目录对象
DIR    Sd_dir;
// 文件对象
FIL    Sd_f0;
// FATFS 写入字节数返回值
UINT   Byte_n;

// 图片长度超范围标志（实际未使用，保留字段）
u8 Frame_out_range = 0;

// SD 卡写入缓冲（20064 字节，按 4 字节对齐）
u8 SD_data_buf[20064] __attribute__((aligned(4)));


// 获取 SD 卡剩余容量（扇区数）
DWORD Getfree_Storage = 0x00;

// 文件系统头部模板（20064 字节，含 128 字节头部 + 686 字节 JPEG header）
// 注意：这是单帧模板，写 10 遍组成 200640 字节的文件头
const u8 fileHeaderBuff[FILE_HEADER_BUFF_SIZE] __attribute__((aligned(4))) =
{
    // ──────── 128 字节预头部 ────────
    'D', 'S', 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,   // 字符串标记 "DS" + 8 字节 0
    0xFF, 0x55, 0x12, 0x34, 0xAB, 0xDC,                                // 文件头标志
    0x02,                                                              // 文件格式版本号 V2.0
    (u8)FRAME_SIZE, (u8)(FRAME_SIZE >> 8), (u8)(FRAME_SIZE >> 16),      // FRAME_SIZE (20064) 小端 3 字节
    (u8)FRAME_HEADER_SIZE, (u8)(FRAME_HEADER_SIZE >> 8),                 // FRAME_HEADER_SIZE (128) 小端 2 字节
    0x00,                                                              // 胶囊类型：0x00 肠，0x01 胃
    (u8)FILE_HEADER_SIZE, (u8)(FILE_HEADER_SIZE >> 8),                   // FILE_HEADER_SIZE (200640) 小端 3 字节
    (u8)(FILE_HEADER_SIZE >> 16),
};



/*
********************************************************************************
* Function Name : SD_Card_Open_File
* Description   : 初始化 SD 卡并打开/创建默认文件
* Paramter      : None
* Return        : None
*
* 初始化流程：
*   ① 重试 3 次：
*        - f_mount()：挂载 FATFS 到 "0:/"
*        - f_opendir()：打开根目录
*   ② 失败时 delay_ms(500) 重试
*   ③ 三次都失败 → NVIC_SystemReset()（系统复位）
*
* 注意：原工程代码注释提到 "TY.JDF" 和 "default.JDF" 文件名，但实际
*       现在的实现使用 capsule_sn 命名（SD_Card_Open_Capsule_File）。
*       本函数只是初始化 SD 卡底层驱动。
********************************************************************************
*/
void SD_Card_Open_File(void)
{
  u8 i;
  for (i = 0; i < 3; i++)
  {
      // ① 包含 SD 卡底层初始化（SDIO 时钟 / GPIO / 卡检测）
      Sd_fr = f_mount(&Sd_fs, "0:/", 1);
      if (Sd_fr != FR_OK) {}

      // ② 打开根目录（验证挂载成功）
      Sd_fr = f_opendir(&Sd_dir, "0:/");
      if (Sd_fr != FR_OK) {}

//      //TF 卡容量不足报警（原工程保留，已注释）
//      Alarm_Card_Free_Capacity();

//      Sd_fr = f_open(&Sd_f0, "0:default.JDF", FA_OPEN_ALWAYS | FA_WRITE);
//      Sd_fr = f_lseek(&Sd_f0, Sd_f0.fsize);

      // 成功则返回
      if (Sd_fr == FR_OK)
      {
        return ;
      }

      // 失败延时 500ms 后重试
      delay_ms(500);
   }

   // 三次都失败 → 系统复位（防止永久死机）
   if (Sd_fr != FR_OK)
   {
      NVIC_SystemReset();
   }

   return ;
}



/*
********************************************************************************
* Function Name : SD_Card_write_data
* Description   : 写入一帧图像数据到 SD 卡
* Paramter      : - array: 数据指针（通常是 Data_Buf0）
*                 - wr_len: 写入字节数（FRAME_SIZE = 20064）
* Return        : None
*
* 写入流程：
*   ① 检查文件大小是否超限（< 0x75B81BC0 ≈ 1.97 GB）
*   ② f_lseek()：定位到文件末尾（追加模式）
*   ③ f_write()：写入数据
*
* 注意：调用前必须先 f_open() 打开文件
********************************************************************************
*/
static void SD_Card_write_data(u8 *array, u16 wr_len)
{
  // 文件大小限制（≈1.97 GB）
  if (Sd_f0.fsize < 0x75B81BC0)
  {
    // 定位到文件末尾（追加）
    Sd_fr = f_lseek(&Sd_f0, Sd_f0.fsize);
    // 写入数据
    Sd_fr = f_write(&Sd_f0, array, wr_len, &Byte_n);
  }

  return ;
}



/*
********************************************************************************
* Function Name : SD_Card_write_head
* Description   : 写入文件头数据（20064 字节 / 次）
* Paramter      : - array: 数据指针（fileHeaderBuff）
*                 - wr_len: 写入字节数（FILE_HEADER_BUFF_SIZE）
* Return        : None
*
* 写入流程：
*   ① f_lseek()：定位到文件末尾
*   ② f_write()：写入头部模板
*
* 调用链：SD_Card_write_file_header() 循环调用 10 次写入 200640 字节
********************************************************************************
*/
static void SD_Card_write_head(u8 *array, u16 wr_len)
{
	// 定位到文件末尾
	Sd_fr = f_lseek(&Sd_f0, Sd_f0.fsize);
	// 写入数据
	Sd_fr = f_write(&Sd_f0, array, wr_len, &Byte_n);

  return ;
}



/*
********************************************************************************
* Function Name : SD_Card_flush
* Description   : 同步缓存到 SD 卡（确保数据写入物理存储）
* Paramter      : None
* Return        : None
*
* 用途：
*   - 掉电保护：防止缓存数据丢失
*   - 调用时机：每 10 帧图像后（性能与安全平衡）
*
* 失败处理：
*   - f_sync 失败 → 重新打开胶囊文件（可能是文件系统异常）
********************************************************************************
*/
static void SD_Card_flush(void)
{
  // 同步缓存到 SD 卡
  Sd_fr = f_sync(&Sd_f0);

  // 如果同步失败，重新初始化文件（可能是文件系统异常）
  if (Sd_fr != FR_OK)
  {
    //SD_Card_Open_File();  // 原工程已注释，改用胶囊文件
    SD_Card_Open_Capsule_File(&DeviceInfo[11]);
  }

  return ;
}



/*
********************************************************************************
* Function Name : SD_Card_write_file_header
* Description   : 写入完整文件头（200640 字节 = 10 × 20064）
* Paramter      : None
* Return        : None
*
* 写入策略：
*   - 只在文件为空时（fsize < FILE_HEADER_SIZE）写入
*   - 每次写 20064 字节（FILE_HEADER_BUFF_SIZE），喂狗后延时 5ms
*   - 10 次循环写完整个文件头
*
* 为什么分 10 次：
*   - 单次写 200640 字节可能超时
*   - 每次写完喂狗防止 IWDG 复位
********************************************************************************
*/
void SD_Card_write_file_header(void)
{
  u32 i;

  // 慢速 SD 卡写完缓存后立即冲洗可能有问题，先延时
  delay_ms(10);
  Iwdt_FeedDog();

  // 判断是否已写过文件头
  if (Sd_f0.fsize < FILE_HEADER_SIZE)
  {
    // 分 10 次写 200640 字节头部
    for (i = 0; i < (FILE_HEADER_SIZE / FILE_HEADER_BUFF_SIZE); i++)
    {
      SD_Card_write_head((u8*)fileHeaderBuff, FILE_HEADER_BUFF_SIZE);
      Iwdt_FeedDog();
      delay_ms(5);     // 给 SD 卡控制器留写入时间
    }

    delay_ms(10);

    // 立即 sync 确保文件头写入
    SD_Card_flush();
  }

  return ;
}



/*
********************************************************************************
* Function Name : SD_Card_storage_imgdata
* Description   : 存储一帧完整图像到 SD 卡（核心写盘函数）
* Paramter      : None
* Return        : None
*
* 调用时机：do_main() 主循环中，收到完整图像后立即调用
*
* 处理流程：
*   ① 更新 DeviceInfo 中的元数据：
*        - DeviceInfo[45..47] = 系统时间戳（秒数）
*        - DeviceInfo[65..67] = 当前图像数据长度
*        - DeviceInfo[70..76] = RTC 时间（BCD）
*   ② 把 DeviceInfo[128B] 复制到 Send_Buf[0..127]
*        - 覆盖预头部，覆盖帧的 DeviceInfo 在 SD 卡写入时的位置
*   ③ Img_process_finsh = 1（写盘期间不接受 UART2 命令）
*   ④ SD_Card_write_data(Send_Buf, FRAME_SIZE)
*        - 写入 20064 字节（128 DevInfo + 686 JPEG header + 19250 JPEG data）
*   ⑤ Img_process_finsh = 0
*
* SD 卡帧布局（每帧 20064 字节）：
*   [0..127]           ← DeviceInfo（本次更新）
*   [128..813]         ← JPEG header（FF D8 ... FF D9）
*   [814..20063]       ← JPEG 压缩数据
****************************************************************************
*/
void SD_Card_storage_imgdata(void)
{
  u8 i;

  // ① 更新 DeviceInfo 元数据
  // 系统时间戳（秒数，3 字节小端）
  DeviceInfo[45] = For_time_now;
  DeviceInfo[46] = For_time_now >> 8;
  DeviceInfo[47] = For_time_now >> 16;

  // 当前图像数据长度（3 字节小端）
  DeviceInfo[65] = Send_data_len;
  DeviceInfo[66] = Send_data_len >> 8;
  DeviceInfo[67] = Send_data_len >> 16;

  // RTC 时间（BCD 格式：秒/分/时/周/日/月/年）
  DeviceInfo[76] = TimeNow.sec;
  DeviceInfo[75] = TimeNow.min;
  DeviceInfo[74] = TimeNow.hour & 0x7F;  // 高位清零（bit7 是 12/24h 标志）
  DeviceInfo[73] = TimeNow.wday;
  DeviceInfo[72] = TimeNow.mday;
  DeviceInfo[71] = TimeNow.month;
  DeviceInfo[70] = (u8)TimeNow.year;

  // ② 把 DeviceInfo 复制到 Send_Buf 前 128 字节（覆盖预头部）
  for (i = 0; i < DEVICE_INFO_LEN; i++)
  {
    Send_Buf[i] = DeviceInfo[i];
  }

  // ③ 写盘期间不接受 UART2 命令
  Img_process_finsh = 1;

  // ④ 写入 SD 卡（20064 字节：128 DevInfo + 686 header + 19250 data）
  SD_Card_write_data(Send_Buf, FRAME_SIZE);

  // ⑤ 写盘完成，恢复 UART2 接收
  Img_process_finsh = 0;

  return ;
}



/*
********************************************************************************
* Function Name : SD_Card_storage_10_flush
* Description   : 每 10 帧图像执行一次 fsync（性能优化）
* Paramter      : None
* Return        : None
*
* 为什么每 10 帧 sync 一次：
*   - 每帧 sync 性能太差（SD 卡 sync 通常 ~100ms）
*   - 掉电时最多丢失 9 帧（在可接受范围）
*   - 提高吞吐量约 10 倍
*
* 调用时机：do_main() 中 send_image_to_PC() 之后
********************************************************************************
*/
void SD_Card_storage_10_flush(void)
{
  // 静态计数器（函数返回后保持值）
  static u8 Frame_cnt = 0;

  Frame_cnt++;
  if (Frame_cnt > 9)
  {
    Frame_cnt = 0;
    SD_Card_flush();  // fsync 到物理存储
  }
}



/*
********************************************************************************
* Function Name : SD_Card_Open_Capsule_File
* Description   : 创建/打开以胶囊 SN 命名的 .YS 文件
* Paramter      : - tempoint: 胶囊 SN 指针（DeviceInfo[11..18]，8 字节）
* Return        : None
*
* 文件命名格式：
*   "0:Y%02x%02x%02x%02x%02x%02x%02x.YS"
*   例：tempoint = 11 22 33 44 55 66 77 88
*       → 文件名 "0:Y1122334455667788.YS"
*
* 注意：使用 tempoint[1..7] 共 7 字节（跳过 tempoint[0] = DeviceInfo[11]）
*       这是因为历史代码习惯，从 SN 第 2 字节开始
*
* 重试机制：
*   - 失败时 delay_ms(500) 重试
*   - 三次都失败 → NVIC_SystemReset()
********************************************************************************
*/
void SD_Card_Open_Capsule_File(u8 *tempoint)
{
  u8 i;
  char Capsule_fname[30] = {NULL};

  // 拼装文件名："0:Y<SN>.YS"
  // 注意：使用 tempoint[1..7]，即 SN 第 2~8 字节
  sprintf(Capsule_fname, "0:Y%02x%02x%02x%02x%02x%02x%02x.YS",
          tempoint[1], tempoint[2], tempoint[3], tempoint[4],
          tempoint[5], tempoint[6], tempoint[7]);

  // 重试 3 次打开/创建文件
  for (i = 0; i < 3; i++)
  {
    // 以读写模式打开（不存在则创建）
    Sd_fr = f_open(&Sd_f0, Capsule_fname, FA_READ | FA_OPEN_ALWAYS | FA_WRITE);
    // 定位到文件末尾（追加模式）
    Sd_fr = f_lseek(&Sd_f0, Sd_f0.fsize);

    // 成功则返回
    if (Sd_fr == FR_OK)
    {
      return ;
    }
    // 失败延时 500ms 重试
    delay_ms(500);
  }

  // 三次都失败 → 系统复位
  if (Sd_fr != FR_OK)
  {
    NVIC_SystemReset();
  }

  return ;
}



/*
********************************************************************************
* Function Name : SD_Card_Close_Capsule_File
* Description   : 关闭胶囊文件并清零文件大小计数
* Paramter      : None
* Return        : None
*
* 调用时机：胶囊文件重新打开前（capsule_sn_CMDDeal() 中）
********************************************************************************
*/
void SD_Card_Close_Capsule_File(void)
{
  // 关闭文件
  Sd_fr = f_close(&Sd_f0);
  // 清零文件大小计数（下次 f_lseek 时重新获取）
  Sd_f0.fsize = 0;

  return ;
}



/*
********************************************************************************
* Function Name : Get_Card_Free_Capacity
* Description   : 获取 SD 卡剩余容量并通过 UART3 发送信息给 PC
* Paramter      : None
* Return        : 剩余容量（字节）
*
* 发送格式：
*   "TFC:<总容量> KB\r\n"    （TFC = Total Free Capacity）
*   "TFFC:<剩余> B\r\n"      （TFFC = Total Free Free Capacity）
*
* 调用时机：PC 命令请求时（通过 UART3 cmd 0x98 文本帧）
********************************************************************************
*/
DWORD Get_Card_Free_Capacity(void)
{
  FATFS *pfs;
  DWORD fre_clust, fre_sect, tot_sect;
  char tembuff[30] = {0};

  // 获取空闲簇数
  f_getfree("0:/", &fre_clust, &pfs);

  // 总扇区数 = (总簇数 - 2) × 每簇扇区数（FAT 表占用 2 个簇）
  tot_sect = (pfs->n_fatent - 2) * pfs->csize;
  // 剩余扇区数 = 空闲簇数 × 每簇扇区数
  fre_sect = fre_clust * pfs->csize;

  // 发送总容量（KB）
  sprintf(tembuff, "TFC:%10lu KB\r\n", tot_sect * 512 / 1024);
  USART3_Send(98, sizeof(tembuff), (u8*)tembuff);

  // 发送剩余容量（B）
  sprintf(tembuff, "TFFC:%10lu B\r\n", fre_sect * 512);
  USART3_Send(98, sizeof(tembuff), (u8*)tembuff);

  return fre_sect * 512;
}



/*
********************************************************************************
* Function Name : scan_dele_files
* Description   : 遍历并删除 SD 卡根目录下所有文件
* Paramter      : None
* Return        : None
*
* 警告：此函数会删除 SD 卡上所有文件，请谨慎使用
*
* 调用时机：仅用于格式化场景（一般不调用）
********************************************************************************
*/
void scan_dele_files(void)
{
  FILINFO fileinfo;
  char *fn;
  char lname[_MAX_LFN * 2 + 1] = {0};

  // 喂狗（长操作）
  Iwdt_FeedDog();

  // 关闭当前文件
  f_close(&Sd_f0);

#if _USE_LFN
  // 长文件名支持
  fileinfo.lfsize = _MAX_LFN * 2 + 1;
  fileinfo.lfname = lname;
#endif

  // 遍历删除所有文件
  while (f_readdir(&Sd_dir, &fileinfo) == FR_OK)
  {
    // fname[0] == 0 表示遍历结束
    if (!fileinfo.fname[0]) break;

#if _USE_LFN
    // 优先使用长文件名
    fn = *fileinfo.lfname ? fileinfo.lfname : fileinfo.fname;
#else
    fn = fileinfo.fname;
#endif

    // 删除文件
    f_unlink(fn);
  }

  return ;
}



/*
********************************************************************************
* Function Name : Alarm_Card_Free_Capacity
* Description   : SD 卡剩余容量 < 5GB 时持续蜂鸣器报警
* Paramter      : None
* Return        : None (会无限循环，除非手动停止)
*
* 报警策略：
*   - 每 2000ms 响一次，每次 10ms
*   - 蜂鸣器响 10ms 后关闭
*   - 持续报警直到系统断电
*
* 调用时机：SD_Card_Open_File() 中（当前已注释）
********************************************************************************
*/
void Alarm_Card_Free_Capacity(void)
{
  FATFS *pfs;
  DWORD fre_clust, fre_sect;

  // 蜂鸣器状态变量
  u32 tf_alarm_beep_on_time_flag = 0;  // 蜂鸣器开启时刻
  u32 tf_alarm_beep_time_flag = 0;     // 蜂鸣器响持续时刻

  // 获取空闲扇区
  f_getfree("0:/", &fre_clust, &pfs);
  fre_sect = fre_clust * pfs->csize;

  // 剩余 < 5GB → 持续报警
  if (fre_sect < fre_sect_num)
  {
    while (1)
    {
      // 每 2000ms 开启一次蜂鸣器
      if ((get_sys_tick_time() - tf_alarm_beep_on_time_flag) > tf_alarm_beep_on_cycly_ms)
      {
        tf_alarm_beep_on_time_flag = get_sys_tick_time();
        Beep_On();                                            // 蜂鸣器开启
        tf_alarm_beep_time_flag = get_sys_tick_time();
      }

      // 蜂鸣器响 10ms 后关闭
      if ((get_sys_tick_time() - tf_alarm_beep_time_flag) > tf_alarm_beep_on_delay_ms)
      {
        Beep_Off();
        tf_alarm_beep_time_flag = 0;
      }
    }
  }

  return ;
}



/*
********************************************************************************
* Function Name : SD_Card_read_rtc_data
* Description   : 从 SD 卡文件读取 RTC 上次更新时间（内部辅助）
* Paramter      : - array: 读取目标缓冲
*                 - wr_len: 读取字节数
*                 - adrr: 文件内偏移地址
* Return        : TRUE (固定返回)
*
* 内部辅助：被 Read_Rtc_From_SD_Card() 调用
********************************************************************************
*/
static BOOL SD_Card_read_rtc_data(u8 *array, u16 wr_len, u8 adrr)
{
  // 定位到指定地址
  Sd_fr = f_lseek(&Sd_f0, adrr);
  // 读取数据
  Sd_fr = f_read(&Sd_f0, array, wr_len, &Byte_n);
  // 返回文件起始位置（避免影响后续追加）
  Sd_fr = f_lseek(&Sd_f0, 0);

  return TRUE;
}



/*
********************************************************************************
* Function Name : Read_Rtc_From_SD_Card
* Description   : 从 SD 卡读取 RTC 上次更新时间（断电保持）
* Paramter      : None
* Return        : None
*
* 读取策略：
*   - 仅当文件已有图像数据（fsize > 20064）时读取
*   - 读取位置：文件内偏移 83（RTC_Uptime_Adr）
*   - 读取 7 字节到 DeviceInfo[83..89]
*
* 调用时机：第一次收到图像时（创建新胶囊文件后）
********************************************************************************
*/
void Read_Rtc_From_SD_Card(void)
{
  // 没有图像数据时跳过（避免空文件读取错误）
  if (Sd_f0.fsize > 20064)
  {
    // 从偏移 83 读 7 字节到 DeviceInfo[83..89]
    SD_Card_read_rtc_data(&DeviceInfo[83], 7, RTC_Uptime_Adr);
  }
}



/*
********************************************************************************
* Function Name : Detect_Bind_Capsule_File
* Description   : 检测是否已绑定胶囊文件（即文件非空）
* Paramter      : None
* Return        : TRUE:  已绑定（文件大小 > 0）
*                 FALSE: 未绑定（文件大小 = 0）
********************************************************************************
*/
BOOL Detect_Bind_Capsule_File(void)
{
  if (Sd_f0.fsize > 0)
  {
    return TRUE;  // 文件非空 = 已绑定
  }
  else
  {
    return FALSE; // 文件为空 = 未绑定
  }
}
