/*
****************************************************************************
* Copyright(C): TY Technical
* FileName    : usart_transfer_station.c
* Author      : TY Technical Software Development Team
* Description : UART2 中转站（PC ↔ 主控板 ↔ RF 板 SN 绑定命令）
*
* 数据流：
*   PC  ──UART2──>  STM32  ──UART2──>  RF 板（nRF52 RX）
*   PC  <──UART2──  STM32  <──UART2──  RF 板
*
* 主要功能：胶囊序列号（SN）绑定相关命令转发
*   - CASE_REQ_CAPSULE_SN：PC 请求 RF 板发送 SN
*   - CASE_RSP_CAPSULE_SN：RF 板响应 SN 给 PC
*
* 注：本工程不直接处理 RF 链路命令，仅作 UART2 透传
****************************************************************************
*/
#include "usart_transfer_station.h"
#include "drv_usart.h"
#include "receive_send_image.h"
#include "storage_card.h"
#include "do_main.h"

// 接收缓冲（最长 20 字节）
#define CAPSULE_ZISE  20

// RF 板响应缓冲（用于转发到 PC）
u8 Capsule_Ack[CAPSULE_ZISE] = {0};

// RF 板应答完成标志
u8 Capsule_ReqAckOKFlag = 0;

// RF 板响应数据长度
u8 Capsule_DataLen = 0;

// 胶囊数据处理完成标志（在 capsule_sn_CMDDeal 中切换状态机）
u8 Capsule_process_flag = 0;



/*
********************************************************************************
* Function Name : capsule_sn_CMDDeal
* Description   : 胶囊 SN 命令处理（PC ↔ RF 板中转）
* Paramter      : None
* Return        : None
*
* 调用时机：do_main() 主循环每轮调用一次
*
* 处理逻辑：
*   ① 检查 Capsule_ReqAckOKFlag：
*        - 0 = RF 板还没响应，直接返回
*        - 1 = RF 板响应已准备好，进入处理
*
*   ② 设置 Img_process_finsh = 1
*        - 标记正在处理 SN 命令，期间暂停图像处理
*
*   ③ 重置 rx_step2（UART2 接收状态机回到起始）
*
*   ④ 根据 Capsule_process_flag 分发：
*        - CASE_REQ_CAPSULE_SN：把 PC 请求转发给 RF 板
*        - CASE_RSP_CAPSULE_SN：把 RF 板响应转发给 PC
*
*   ⑤ 清除所有标志，准备下一次处理
*
* 注：本函数实际不调用 RF 板命令（依赖外部 USART2_Receive 设置标志）
*       仅作为状态机分发器
********************************************************************************
*/
void capsule_sn_CMDDeal(void)
{
    // RF 板还没响应 → 直接返回
    if (!Capsule_ReqAckOKFlag)
    {
        return ;
    }

    // 标记正在处理 SN 命令（暂停图像处理避免 UART2 数据冲突）
    Img_process_finsh = 1;

    // 重置 UART2 接收状态机
    rx_step2 = 0;

    // 根据当前阶段分发处理
    switch (Capsule_process_flag)
    {
        // CASE 1：PC 请求查询/解绑/绑定 → 转发给 RF 板
        case CASE_REQ_CAPSULE_SN:
            // 如果当前有打开的胶囊文件，先关闭（避免冲突）
            if (!Capsule_File_FLAG)
            {
                SD_Card_Close_Capsule_File();
            }
            // 通过 UART2 把 PC 的请求数据原样转发给 RF 板
            USART2_Send(Capsule_ReqAckOKFlag, Capsule_DataLen, Usart1_rec_buf);
            Capsule_DataLen = 0;
            Capsule_process_flag = 0;
            break;

        // CASE 2：RF 板响应 → 转发给 PC
        case CASE_RSP_CAPSULE_SN:
            // 通过 UART3 把 RF 板的响应数据转发给 PC
            USART3_Send(Capsule_ReqAckOKFlag, Capsule_DataLen, Capsule_Ack);
            Capsule_DataLen = 0;
            Capsule_process_flag = 0;
            break;

        default:
            break;
    }

    // 清除应答完成标志
    Capsule_ReqAckOKFlag = 0;

    // 恢复图像处理
    Img_process_finsh = 0;

    return ;
}
