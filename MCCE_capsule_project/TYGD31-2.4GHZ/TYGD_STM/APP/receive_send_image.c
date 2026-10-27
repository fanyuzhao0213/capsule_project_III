/*
****************************************************************************
* Copyright(C): TY Technical
* FileName    : receive_send_image.c
* Author      : TY Technical Software Development Team
* Description : 从 RF 接收板接收图像 + 转发到 PC / 存储到 SD 卡
*
* 完整数据流：
*   ① USART3 中断接收 RF 板的图像帧（带 CRC8 校验）
*   ② 接收到完整一帧后置位 Img_data_reday
*   ③ receive_image_data_end() 检查标志 → 调用本文件的处理
*   ④ device_info_handle() 从缓冲区末尾提取 64 字节 DeviceInfo
*   ⑤ send_image_to_PC() 拼装 UART3 payload 转发给 PC
*
* 缓冲区布局（Data_Buf0，BUFF_SIZE=20264）：
*   [0..127]            预头部（运行时被 DeviceInfo 覆盖）
*   [128..813]          JPEG header（FF D8 ... SOS）
*   [814..814+N-1]      JPEG 数据（N = Send_data_len，从 RF 接收）
*   [814+N..814+N+685]   重复的 JPEG header（运行时保留）
*   [814+N+686..814+N+813] 64 字节 DeviceInfo（从 RF 板复制）
****************************************************************************
*/
#include "receive_send_image.h"
#include "device_info.h"
#include "drv_usart.h"
#include "jpeg_head.h"
#include "time_handle.h"

// RF 接收数据缓冲区指针（指向 Data_Buf0，参考点 ZKB）
u8* Rec_Buf;

// PC 发送数据缓冲区指针（指向 Data_Buf0，与 Rec_Buf 共享）
u8* Send_Buf;

// 设备信息缓冲（128 字节）
u8 DeviceInfo[DEVICE_INFO_LEN] = {0};

// 图像数据已接收完成标志（UART3 中断置位，主循环检查并清除）
u8 Img_data_reday = 0;

// 发送/已接收的图像数据字节数（= JPEG 压缩数据长度，不含 header）
u32 Send_data_len = 0;

// 图像处理进行中标志（接收/存储图像时置 1，期间不接受 UART2 命令）
u8 Img_process_finsh = 0;



/*
********************************************************************************
* Function Name : device_info_handle
* Description   : 从 Rec_Buf 末尾提取 DeviceInfo 后 64 字节并填入 DeviceInfo[1..64]
* Paramter      : - rec_buf: 接收缓冲区指针（Data_Buf0）
* Return        : None
*
* 提取原理：
*   已知 Rec_Buf 末尾 128 字节结构（从 device_info_handle 旧注释推断）：
*     [Send_data_len + 686 .. Send_data_len + 811]  ← 64 字节 DeviceInfo 数据
*
*   rec_buf_num 计算：
*     rec_buf_num = Send_data_len + IMAGE_HEADER_LEN + FRAME_HEADER_SIZE - 2
*                = Send_data_len + 686 + 128 - 2
*                = Send_data_len + 812
*     rec_buf_num = rec_buf_num - 64
*                = Send_data_len + 748
*
*   提取 64 字节（倒序）：
*     DeviceInfo[i] = Rec_Buf[rec_buf_num--]
*     其中 i 从 64 递减到 1（DeviceInfo[0] 保留为标记字节）
*
* 调用链：receive_image_data_end() → device_info_handle() → device_info_rewrite()
****************************************************************************
*/
static void device_info_handle(u8 *rec_buf)
{
  u8 i;
  u32 rec_buf_num = 0;

  // 参数检查
  if (NULL == rec_buf)
  {
    return ;
  }

  // 计算 64 字节 DeviceInfo 在 Rec_Buf 中的起始位置
  rec_buf_num = Send_data_len;                          // JPEG 数据末尾
  rec_buf_num += IMAGE_HEADER_LEN;                      // 跳过 JPEG header (686)
  rec_buf_num += FRAME_HEADER_SIZE;                     // 跳过头部 (128)
  rec_buf_num -= 2;                                      // 微调 2 字节（适配实际位置）

  // 从 rec_buf_num - 64 开始倒序读 64 字节到 DeviceInfo[1..64]
  rec_buf_num = rec_buf_num - 64;
  for (i = DEVICE_INFO_LEN - 64; i > 0; i--)
  {
    DeviceInfo[i] = rec_buf[rec_buf_num--];
  }

  // 重写 DeviceInfo（填充 MCU ID / 版本号 / 头部标记等）
  device_info_rewrite(DeviceInfo);

  return ;
}



/*
********************************************************************************
* Function Name : receive_image_data_end
* Description   : 检查并处理一帧完整图像
* Paramter      : None
* Return        : TRUE:  处理完成一帧新图像（Send_Buf 已就绪）
*                 FALSE: 还没有完整图像（继续等待）
*
* 调用时机：do_main() 主循环，每轮调用一次
*
* 处理流程：
*   ① if (!Img_data_reday) → 直接返回 FALSE（不阻塞）
*   ② device_info_handle(Rec_Buf)
*        - 从 Rec_Buf 末尾提取 64 字节 DeviceInfo
*        - 调用 device_info_rewrite() 填充 MCU ID / 版本号 / 头部标记
*   ③ Send_Buf = Rec_Buf
*        - Rec_Buf 与 Send_Buf 共享 Data_Buf0
*   ④ return TRUE
*
* 注意：
*   - Img_data_reday 由 UART3 中断置位，本函数不清除
*   - 清除在 send_image_to_PC() 中（发送完成后清除）
*   - Send_data_len 在 send_image_to_PC() 末尾清零
****************************************************************************
*/
BOOL receive_image_data_end(void)
{
  // 图像数据还没接收完成（UART3 中断未置位 Img_data_reday）
  if (!Img_data_reday)
  {
    return FALSE;
  }

  // 处理 DeviceInfo：从 Rec_Buf 末尾提取并重写
  device_info_handle(Rec_Buf);

  // Send_Buf 指向 Rec_Buf（共享 Data_Buf0）
  Send_Buf = Rec_Buf;

  return TRUE;
}



/*
********************************************************************************
* Function Name : send_image_to_PC
* Description   : 把当前图像通过 UART3 转发给 PC（实时显示）
* Paramter      : - cmd: 命令字（必须为 CMD_IMG_FORWARD = 0x81）
* Return        : None
*
* 拼装 UART3 payload（写入 Send_Buf 后的调整）：
*   ① Send_data_len += IMAGE_HEADER_LEN (686)
*        - 把 JPEG header 长度算入总长
*   ② Send_data_len += FRAME_HEADER_SIZE (128)
*        - 把 DeviceInfo 长度算入总长
*   ③ 复制 DeviceInfo[128B] 到 Send_Buf[Send_data_len..]
*        - 把最新 DeviceInfo 覆盖到 payload 末尾
*   ④ USART3_Send(cmd, Send_data_len, &Send_Buf[FRAME_HEADER_SIZE])
*        - 跳过前 128 字节预头部
*        - payload 长度 = 原 JPEG 数据 + 686 + 128
*
* UART3 帧格式：
*   [FF 55 12 34][cmd][lenHI][lenLO][payload][CRC8]
*   其中 payload = Send_Buf[128..127+Send_data_len]
*
* 后置清理：
*   - Img_data_reday = 0     （清除接收完成标志）
*   - Send_data_len = 0       （清零长度）
****************************************************************************
*/
void send_image_to_PC(u8 cmd)
{
	u16 i;

	// ① 把 JPEG header 长度 686 加入总长
	Send_data_len += IMAGE_HEADER_LEN;

	// ② 把 DeviceInfo 长度 128 加入总长
	Send_data_len += FRAME_HEADER_SIZE;

	// ③ 把 DeviceInfo 复制到 Send_Buf 末尾（覆盖 Rec_Buf 中残留的旧 DeviceInfo）
	for (i = 0; i < FRAME_HEADER_SIZE; i++)
	{
		Send_Buf[Send_data_len + i] = DeviceInfo[i];
	}

	// ④ 通过 UART3 发送（跳过 Send_Buf 前 128 字节预头部）
	USART3_Send(cmd, Send_data_len, &Send_Buf[FRAME_HEADER_SIZE]);

	// 清除图像接收完成标志
	Img_data_reday = 0;

	// 清零图像数据长度（准备下一帧）
	Send_data_len = 0;

	return ;
}
