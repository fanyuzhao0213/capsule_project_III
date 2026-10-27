/*
****************************************************************************
* Copyright(C): TY Technology
* FileName    : protocol.h
* Description : 通信协议
****************************************************************************
*/

#ifndef _PROTOCOL_H_
#define _PROTOCOL_H_



// 图像发送命令，胶囊->RF
#define CMD_IMG_BEGIN_PACKET        0x01    // 图像开始包
#define CMD_IMG_END_PACKET          0x03    // 图像结束包
#define CMD_IMG_DATA_PACKET         0x80    // 图像数据包

// 图像转发命令，RF->主控->PC
#define CMD_IMG_FORWARD             0x81    // 转发图像

// 图像接收应答命令，RF->胶囊
#define CMD_IMG_RCVD_RSP            0x10    // 图像接收应答


// 胶囊序列号广播命令，胶囊->RF->主控->PC
#define CMD_CAPSULE_SN_BROADCAST    0x05    // 广播胶囊序列号

// 胶囊ID号广播命令，胶囊->RF->主控->PC
#define CMD_CAPSULE_ID_BROADCAST    0x07   // 广播胶囊ID号


// 胶囊序列号绑定相关命令，PC<->主控<->RF
#define CMD_REQ_CAPSULE_SN_QUERY    0x20    // 已绑定胶囊序列号查询请求
#define CMD_RSP_CAPSULE_SN_QUERY    0x21    // 已绑定胶囊序列号查询应答
#define CMD_REQ_CAPSULE_SN_UNBIND   0x22    // 胶囊序列号解绑请求
#define CMD_RSP_CAPSULE_SN_UNBIND   0x23    // 胶囊序列号解绑应答
#define CMD_REQ_CAPSULE_SN_BIND     0x24    // 胶囊序列号绑定请求
#define CMD_RSP_CAPSULE_SN_BIND     0x25    // 胶囊序列号绑定应答

#define CMD_REQ_CAPSULE_SN_SET      0x26    // 胶囊序列号设定请求
#define CMD_RSP_CAPSULE_SN_SET      0x27    // 胶囊序列号设定应答

#define CMD_REQ_CAPSULE_SN_SET_OK      0x28    // 胶囊序列号设定OK请求
#define CMD_RSP_CAPSULE_SN_SET_OK      0x29    // 胶囊序列号设定OK应答

#define CMD_REQ_RADIO_CHANNEL_FREQ_SET     0x2A    // 接收板通信频道设定请求
#define CMD_RSP_RADIO_CHANNEL_FREQ_SET     0x2B    // 接收板通信频道设定应答


#endif /* _PROTOCOL_H_ */

