/*
****************************************************************************
* Copyright(C): TY Technical
* FileName    : device_info.c
* Author      : TY Technical Software Development Team
* Description : 设备信息（DeviceInfo）组装
*
* DeviceInfo 是 128 字节的设备元数据，随每帧图像一起写入 SD 卡
* 并通过 UART3 转发给 PC。DeviceInfo 字段布局：
*
* ┌──────────┬───────┬──────────────────────────────────────┐
* │ 偏移     │ 长度  │ 内容                                  │
* ├──────────┼───────┼──────────────────────────────────────┤
* │ 0~4      │ 5     │ 数据头 00 55 AA 88 99                │
* │ 5~6      │ 2     │ VERSION_MAIN / VERSION_SUB (位置1)   │
* │ 9~10     │ 2     │ VERSION_MAIN / VERSION_SUB (位置2)   │
* │ 11~18    │ 8     │ 胶囊序列号 SN                       │
* │ 27~38    │ 12    │ STM32 MCU 唯一 ID                    │
* │ 45~47    │ 3     │ 系统时间戳（秒数，小端）             │
* │ 61       │ 1     │ VERSION_TEST (位置1，备用)            │
* │ 63       │ 1     │ VERSION_TEST (位置2)                │
* │ 65~67    │ 3     │ 当前帧图像数据长度（小端）           │
* │ 70~76    │ 7     │ RTC 时间（BCD）                     │
* │ 83~89    │ 7     │ RTC 上次更新时间（SD 卡持久化）     │
* └──────────┴───────┴──────────────────────────────────────┘
****************************************************************************
*/
#include "device_info.h"

#include "drv_sd2058.h"
#include "time_handle.h"

// STM32 芯片 ID 寄存器地址（96 位唯一 ID）
static u8 *MCU_id = (u8 *)0x1FFFF7E8;



/*
********************************************************************************
* Function Name : MCU_info_copy
* Description   : 把 STM32 MCU 唯一 ID（12 字节）写入 DeviceInfo[27..38]
* Paramter      : - buff_info: DeviceInfo 缓冲指针
* Return        : None
*
* STM32 唯一 ID 位置：
*   - 地址 0x1FFFF7E8（96 位 = 12 字节）
*   - 全局唯一，不可修改
*   - 用于追溯设备身份
*
* 注：原工程使用 MCU_id[0..11]，但实际 STM32 的 ID 是 12 字节（96 bit）
*     buff_info[27..38] 共 12 字节正好容纳
********************************************************************************
*/
static void MCU_info_copy(u8 *buff_info)
{
  // 参数检查
  if (NULL == buff_info)
  {
    return ;
  }

  // 把 MCU 唯一 ID 复制到 DeviceInfo[27..38]
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
* Function Name : version_info_copy
* Description   : 把固件版本号写入 DeviceInfo[5..6] / [9..10] / [63]
* Paramter      : - buff_info: DeviceInfo 缓冲指针
* Return        : None
*
* 写入字段：
*   - DeviceInfo[5]  = VERSION_MAIN（主版本号）
*   - DeviceInfo[6]  = VERSION_SUB （子版本号）
*   - DeviceInfo[9]  = VERSION_MAIN（备用位置 1）
*   - DeviceInfo[10] = VERSION_SUB （备用位置 1）
*   - DeviceInfo[63] = VERSION_TEST（测试版本号）
*
* 双位置备份的原因：
*   - 不同历史版本的固件写入位置不同
*   - PC 端解析器兼容两种位置（详见 PY 解析器 device_info_to_text）
****************************************************************************
*/
static void version_info_copy(u8 *buff_info)
{
  // 参数检查
  if (NULL == buff_info)
  {
    return ;
  }

  // 主版本号（位置1）
  buff_info[9] = VERSION_MAIN;

  // 子版本号（位置1）
  buff_info[10] = VERSION_SUB;

  // 测试版本号
  buff_info[63] = VERSION_TEST;

  return ;
}



/*
********************************************************************************
* Function Name : data_head_info_copy
* Description   : 把数据头标记 00 55 AA 88 99 写入 DeviceInfo[0..4]
* Paramter      : - buff_info: DeviceInfo 缓冲指针
* Return        : None
*
* 头标记作用：
*   - PC 端解析器快速识别 DeviceInfo 起始位置
*   - 调试时可视化检查数据完整性
****************************************************************************
*/
static void data_head_info_copy(u8 *buff_info)
{
  // 参数检查
  if (NULL == buff_info)
  {
    return ;
  }

  // 数据头标记 00 55 AA 88 99
  buff_info[0] = 0x00;
  buff_info[1] = 0x55;
  buff_info[2] = 0xAA;
  buff_info[3] = 0x88;
  buff_info[4] = 0x99;

  return ;
}



/*
********************************************************************************
* Function Name : device_info_rewrite
* Description   : 重新组装 DeviceInfo（清空旧值后填充所有固定字段）
* Paramter      : - buff_info: DeviceInfo 缓冲指针（128 字节）
* Return        : None
*
* 调用时机：
*   - receive_image_data_end() → device_info_handle() 末尾
*   - 从 RF 板接收完一帧后调用，重写 DeviceInfo 固定字段
*
* 写入字段：
*   - [0..4]    数据头标记
*   - [5..6]    / [9..10] 版本号
*   - [27..38]  MCU ID
*   - 其他字段（SN、时间戳等）由 receive_send_image.c 的其他函数写入
*
* 注意：本函数不重置 DeviceInfo 已有的 SN/RTC 时间等动态字段，
*       只填充固定的设备元数据。
****************************************************************************
*/
void device_info_rewrite(u8 *buff_info)
{
  // ① 写入数据头标记
  data_head_info_copy(buff_info);

  // ② 写入版本号
  version_info_copy(buff_info);

  // ③ 写入 MCU 唯一 ID
  MCU_info_copy(buff_info);

  return ;
}
