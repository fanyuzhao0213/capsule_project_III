/**
 ******************************************************************************
 * @file    capsule_protocol.c
 * @brief   STM协议核心：解析RX图片/控制帧、生成.YS记录并转发PC。
 *
 * 两条输入链路：
 *   USART2(nRF RX)
 *      ├─ FF 55 12 34 81图片帧 → 校验/补JPEG头 → SD保存 + USART3 DMA转发
 *      └─ 5A 41 59 53控制应答 → 暂存 → USART3空闲时转发PC
 *   USART3(PC)
 *      ├─ 0x02设置时间命令 → STM本地写SD2058
 *      └─ 其他命令/普通字节 → 阻塞转发USART2给nRF RX
 *
 * 模块状态统一收口到m_protocol：
 *   image_rx    RX图片流状态机和整帧缓存
 *   nrf_control nRF控制应答拼包和pending邮箱
 *   pc_control  PC控制命令拼包状态
 *   pc_tx       USART3五段式图片DMA状态机
 *   record      .YS记录、RTC状态和SD同步节拍
 *   stats       仅用于日志观测的运行统计
 *
 * 设计约束：本文件不再新增独立可变全局标志；新增状态必须归属上述子模块。
 * @version 2.0.0
 ******************************************************************************
 */

#include "capsule_protocol.h"
#include "app_log.h"
#include "app_uart_rx.h"
#include "legacy_jpeg_header.h"
#include "sd2058.h"
#include "sd_storage.h"
#include "usart.h"
#include <stdio.h>
#include <string.h>

/** RX图片帧固定前导码长度：FF 55 12 34 81。 */
#define CAPSULE_FRAME_PREAMBLE_SIZE       5U
/** RX图片帧头总长度：5字节前导码 + 2字节大端Payload长度。 */
#define CAPSULE_FRAME_HEADER_SIZE         7U
/** RX在每张图片前附带的固定设备信息长度。 */
#define CAPSULE_DEVICE_INFO_SIZE        128U
/** PC图像协议中位于JPEG和最终DeviceInfo之间、暂不解析的设备信息段。 */
#define CAPSULE_PC_RESERVED_INFO_SIZE   130U
/** STM允许接收的原始JPEG最大长度，单位：字节。 */
#define CAPSULE_IMAGE_MAX_SIZE        20000U
/** 最大接收帧缓存：Header + DeviceInfo + JPEG + Checksum。 */
#define CAPSULE_FRAME_BUFFER_SIZE     (CAPSULE_FRAME_HEADER_SIZE + \
                                       CAPSULE_DEVICE_INFO_SIZE + \
                                       CAPSULE_IMAGE_MAX_SIZE + 1U)
/** 图片转发命令号；RX→STM图片帧与STM→PC图片帧均使用0x81。 */
#define CAPSULE_CMD_IMAGE_FORWARD       0x81U
/** 阻塞UART控制帧转发的HAL超时；不是图片流解析超时。 */
#define CAPSULE_UART_TIMEOUT_MS         1000U
/** SD卡中每条旧格式.YS记录的固定长度。 */
#define LEGACY_RECORD_SIZE             20064U
/** 固定记录扣除128字节DeviceInfo后，可容纳的最终JPEG长度。 */
#define LEGACY_RECORD_JPEG_CAPACITY    (LEGACY_RECORD_SIZE - \
                                        CAPSULE_DEVICE_INFO_SIZE)
/** 写入DeviceInfo的STM软件版本号。 */
#define STM_VERSION_MAIN                   1U
#define STM_VERSION_SUB                    1U
#define STM_VERSION_TEST                   0U
/** STM32F103 96位芯片唯一ID的系统存储地址。 */
#define STM32_UNIQUE_ID_ADDRESS       0x1FFFF7E8UL
/** ZAYS短控制帧的本地拼包缓存上限。 */
#define CAPSULE_CONTROL_FRAME_MAX             32U
/** 1：每帧在USART1打印STM补齐后的最终128字节DeviceInfo。 */
#define CAPSULE_DEVICE_INFO_LOG_ENABLED         1U

/*
 * TYGD31协议中128字节DeviceInfo的固定字段偏移。
 * RX先填入TX/RX版本、SN、天线和RSSI；STM保留这些内容，只覆盖STM负责字段。
 */
#define DEVICE_INFO_TX_VERSION_MAIN_OFFSET      5U
#define DEVICE_INFO_RX_VERSION_MAIN_OFFSET      7U
#define DEVICE_INFO_STM_VERSION_MAIN_OFFSET     9U
#define DEVICE_INFO_CAPSULE_SN_OFFSET          11U
#define DEVICE_INFO_RX_DEVICE_ID_OFFSET        19U
#define DEVICE_INFO_STM_DEVICE_ID_OFFSET       27U
#define DEVICE_INFO_STM_UPTIME_OFFSET          45U
#define DEVICE_INFO_ACTIVE_ANTENNA_OFFSET      49U
#define DEVICE_INFO_RADIO_FREQUENCY_OFFSET     50U
#define DEVICE_INFO_TX_VERSION_TEST_OFFSET     61U
#define DEVICE_INFO_RX_VERSION_TEST_OFFSET     62U
#define DEVICE_INFO_STM_VERSION_TEST_OFFSET    63U
#define DEVICE_INFO_JPEG_LENGTH_OFFSET         65U
#define DEVICE_INFO_RTC_OFFSET                 70U

typedef enum
{
  CAPSULE_PARSE_PREAMBLE = 0, /**< 空闲态：同时寻找图片帧头和ZAYS控制帧头。 */
  CAPSULE_PARSE_LENGTH,       /**< 已收到5字节图片帧头，等待2字节Payload长度。 */
  CAPSULE_PARSE_BODY          /**< 按声明长度接收DeviceInfo、JPEG和Checksum。 */
} CapsuleParseState_t;

typedef enum
{
  PC_TX_IDLE = 0,       /**< USART3没有图片DMA事务，可接受下一张完整图片。 */
  PC_TX_HEADER,         /**< 正在发送7字节ZAYS/0x81头。 */
  PC_TX_JPEG,           /**< 正在发送最终JPEG。 */
  PC_TX_RESERVED_INFO,  /**< 正在发送130字节暂不解析的设备信息段。 */
  PC_TX_DEVICE_INFO,    /**< 正在发送128字节DeviceInfo。 */
  PC_TX_CHECKSUM        /**< 正在发送末尾1字节累加校验。 */
} PcTxState_t;

/** @brief RX图片流解析状态和累计统计。 */
typedef struct
{
  CapsuleParseState_t state;  /**< 当前图片解析阶段。 */
  uint8_t preamble_index;     /**< 已连续匹配的图片前导码字节数，范围0～5。 */
  uint16_t expected_length;   /**< Header声明的Payload长度，不含Header和Checksum。 */
  uint16_t received_length;   /**< capsule_frame内当前已保存的总字节数。 */
  uint32_t completed_frames;  /**< 已完成校验和后处理的有效图片数量。 */
  uint32_t rejected_frames;   /**< 因长度、校验、标识或资源忙而拒绝的图片数量。 */
  uint32_t stored_frames;     /**< 已成功追加到SD卡.YS文件的记录数量。 */
} CapsuleParser_t;

/** @brief USART2图片接收域：流式解析状态和最大帧拼包区。 */
typedef struct
{
  CapsuleParser_t parser;
  uint8_t frame[CAPSULE_FRAME_BUFFER_SIZE];
} CapsuleImageRxContext_t;

/** @brief 一条ZAYS短控制帧的通用流式拼包状态。 */
typedef struct
{
  uint8_t frame[CAPSULE_CONTROL_FRAME_MAX];
  uint16_t received;
  uint16_t expected;
} CapsuleControlParser_t;

/** @brief nRF→STM控制应答域：拼包状态和等待转发PC的单帧邮箱。 */
typedef struct
{
  CapsuleControlParser_t parser;
  uint8_t pending_frame[CAPSULE_CONTROL_FRAME_MAX];
  uint16_t pending_length;
} CapsuleNrfControlContext_t;

/** @brief STM→PC五段式图片DMA发送域。 */
typedef struct
{
  uint8_t header[CAPSULE_FRAME_HEADER_SIZE];
  uint8_t reserved_info[CAPSULE_PC_RESERVED_INFO_SIZE];
  uint8_t checksum;
  uint16_t jpeg_length;
  volatile PcTxState_t state;
  volatile uint32_t completed_events;
  volatile uint32_t error_events;
  volatile uint32_t completed_total;
} CapsulePcTxContext_t;

/** @brief 最终.YS记录、RTC可用性和SD同步节拍。 */
typedef struct
{
  uint8_t data[LEGACY_RECORD_SIZE];
  uint8_t rtc_available;
  uint8_t records_since_sync;
} CapsuleRecordContext_t;

/** @brief 只用于运行观测的STM有效图片统计。 */
typedef struct
{
  uint32_t valid_frame_count;
  uint32_t last_valid_frame_ms;
  uint8_t have_last_valid_frame;
  uint8_t device_info_logged;
} CapsuleStatistics_t;

/**
 * @brief CapsuleProtocol模块的唯一运行上下文。
 *
 * 所有可变状态均按职责归属到子结构，避免新增裸全局标志位。
 * 大容量数组保持静态分配，不占用线程栈，也不使用动态内存。
 */
typedef struct
{
  CapsuleImageRxContext_t image_rx;
  CapsuleNrfControlContext_t nrf_control;
  CapsuleControlParser_t pc_control;
  CapsulePcTxContext_t pc_tx;
  CapsuleRecordContext_t record;
  CapsuleStatistics_t stats;
} CapsuleProtocolContext_t;

/** RX→STM图片前导码：旧协议同步字节FF 55 12 34 + 图片命令0x81。 */
static const uint8_t capsule_preamble[CAPSULE_FRAME_PREAMBLE_SIZE] =
    {0xFFU, 0x55U, 0x12U, 0x34U, CAPSULE_CMD_IMAGE_FORWARD};
/** 模块唯一状态实例；禁止在本文件继续新增独立的可变全局标志。 */
static CapsuleProtocolContext_t m_protocol;

/** @brief 检查RX生成的128字节设备信息标识，避免把错位数据当成SN/ID。 */
static uint8_t CapsuleProtocol_DeviceInfoIsValid(const uint8_t *device_info)
{
  /* DeviceInfo前5字节必须是固定标识00 55 AA 88 99。 */
  static const uint8_t marker[5] = {0x00U, 0x55U, 0xAAU, 0x88U, 0x99U};
  return (memcmp(device_info, marker, sizeof(marker)) == 0) ? 1U : 0U;
}

/** @brief 以16字节一行打印STM补齐后的最终DeviceInfo，便于按偏移比对。 */
static void CapsuleProtocol_LogDeviceInfo(const uint8_t *device_info)
{
#if CAPSULE_DEVICE_INFO_LOG_ENABLED
  uint16_t offset;
  uint16_t index;
  char line[96];

  /* 只打印第一张有效图片，避免1 Mbit/s日志串口长期占用主循环。 */
  if (m_protocol.stats.device_info_logged != 0U)
  {
    return;
  }
  m_protocol.stats.device_info_logged = 1U;
  LOG_INFO("[DEVICE_INFO STM] first valid frame, final bytes=128");
  for (offset = 0U; offset < CAPSULE_DEVICE_INFO_SIZE; offset += 16U)
  {
    /* 先生成本行偏移前缀，再追加16个十六进制字节。 */
    int used = snprintf(line, sizeof(line), "[I] DI[%03u]:",
                        (unsigned int)offset);
    if (used < 0)
    {
      continue;
    }
    for (index = 0U; index < 16U; ++index)
    {
      int written = snprintf(&line[used], sizeof(line) - (uint16_t)used,
                             " %02X", device_info[offset + index]);
      if ((written < 0) || ((uint16_t)(used + written) >= sizeof(line)))
      {
        break;
      }
      used += written;
    }
    if (((uint16_t)used + 2U) <= sizeof(line))
    {
      /* AppLog_Write要求调用者补齐行结束符。 */
      line[used++] = '\r';
      line[used++] = '\n';
      AppLog_Write((const uint8_t *)line, (uint16_t)used);
    }
  }
#else
  (void)device_info;
#endif
}

/** @brief 记录STM最终有效图片的累计帧号、间隔和实际帧率。 */
static void CapsuleProtocol_LogFrameTiming(uint32_t arrival_ms,
                                           uint16_t raw_length,
                                           uint16_t jpeg_length)
{
  /* 只有通过完整帧校验并进入后处理的图片才计入STM有效帧率。 */
  m_protocol.stats.valid_frame_count++;
  if (m_protocol.stats.have_last_valid_frame != 0U)
  {
    uint32_t interval_ms = arrival_ms - m_protocol.stats.last_valid_frame_ms;
    /* 放大100倍进行整数运算，例如fps_x100=125代表1.25 fps。 */
    uint32_t fps_x100 = (interval_ms != 0U) ?
                       (100000U / interval_ms) : 0U;
    LOG_INFO("[FRAME] STM valid=%lu interval=%lums fps=%lu.%02lu raw=%u jpeg=%u",
             (unsigned long)m_protocol.stats.valid_frame_count,
             (unsigned long)interval_ms,
             (unsigned long)(fps_x100 / 100U),
             (unsigned long)(fps_x100 % 100U),
             (unsigned int)raw_length,
             (unsigned int)jpeg_length);
  }
  else
  {
    LOG_INFO("[FRAME] STM valid=1 first raw=%u jpeg=%u",
             (unsigned int)raw_length, (unsigned int)jpeg_length);
    m_protocol.stats.have_last_valid_frame = 1U;
  }
  m_protocol.stats.last_valid_frame_ms = arrival_ms;
}

/** @brief 计算ZAYS控制帧数据区累加校验和。 */
static uint8_t CapsuleProtocol_ControlChecksum(const uint8_t *data,
                                                uint16_t length)
{
  uint8_t sum = 0U;
  uint16_t index;
  for (index = 0U; index < length; ++index)
  {
    /* uint8_t自然截断高位，实现协议规定的8位累加和。 */
    sum = (uint8_t)(sum + data[index]);
  }
  return sum;
}

/**
 * @brief 从nRF输入流中识别一字节控制应答，并在完整校验后暂存给PC。
 *
 * 简易流程图：
 *   逐字节匹配"ZAYS"
 *       → 收命令和2字节载荷长度
 *       → 按长度收完整帧
 *       → 校验数据区8位累加和
 *       → 成功：复制到pending缓冲；失败：丢弃并记录日志
 *
 * @return 当前字节属于控制帧解析过程返回1，否则返回0。
 * @note 该函数只排队，不在图片DMA占用USART3时强行发送控制应答。
 */
static uint8_t CapsuleProtocol_InputControlByte(uint8_t byte)
{
  static const uint8_t header[4] = {0x5AU, 0x41U, 0x59U, 0x53U};
  CapsuleControlParser_t *parser = &m_protocol.nrf_control.parser;

  /* 第一阶段：逐字节匹配ZAYS四字节同步头。 */
  if (parser->received < 4U)
  {
    if (byte == header[parser->received])
    {
      parser->frame[parser->received++] = byte;
      return 1U;
    }
    /* 失配字节若仍是0x5A，可立即作为下一次ZAYS匹配的新起点。 */
    parser->received = (byte == header[0]) ? 1U : 0U;
    if (parser->received != 0U)
    {
      parser->frame[0] = byte;
      return 1U;
    }
    return 0U;
  }
  if (parser->received >= sizeof(parser->frame))
  {
    /* 防御性检查：任何异常状态都不能继续写出32字节缓存边界。 */
    parser->received = 0U;
    parser->expected = 0U;
    return 1U;
  }
  /* 第二阶段：ZAYS头已确认，继续接收命令、长度、Payload和Checksum。 */
  parser->frame[parser->received++] = byte;
  if (parser->received == 7U)
  {
    /* 完整帧长度 = 4字节ZAYS + 1字节命令 + 2字节长度 + Payload + 1字节校验。 */
    parser->expected = (uint16_t)(8U +
        (((uint16_t)parser->frame[5] << 8) | parser->frame[6]));
    if (parser->expected > sizeof(parser->frame))
    {
      /* 控制帧超过本地上限时立即放弃，等待下一次ZAYS同步头。 */
      parser->received = 0U;
      parser->expected = 0U;
    }
  }
  if ((parser->expected != 0U) && (parser->received == parser->expected))
  {
    /* Checksum只覆盖Payload，不覆盖ZAYS、命令和长度字段。 */
    if (CapsuleProtocol_ControlChecksum(&parser->frame[7],
          (uint16_t)(parser->expected - 8U)) ==
        parser->frame[parser->expected - 1U])
    {
      /* 当前仅保留一帧pending；新应答会替换尚未转发的旧应答。 */
      (void)memcpy(m_protocol.nrf_control.pending_frame, parser->frame,
                   parser->expected);
      m_protocol.nrf_control.pending_length = parser->expected;
      if ((parser->frame[4] == 0x05U) &&
          (parser->expected == 16U))
      {
        LOG_INFO("[DISCOVERY] SN from RX: %02X %02X %02X %02X %02X %02X %02X %02X",
                 parser->frame[7], parser->frame[8], parser->frame[9],
                 parser->frame[10], parser->frame[11], parser->frame[12],
                 parser->frame[13], parser->frame[14]);
      }
    }
    else
    {
      LOG_WARNING("NRF control response checksum failed");
    }
    parser->received = 0U;
    parser->expected = 0U;
  }
  return 1U;
}

/** @brief 清空NRF图片帧解析状态，准备接收下一帧。 */
static void CapsuleProtocol_ResetParser(void)
{
  /* 只复位本帧拼包状态，累计成功/拒绝/存储统计保持不变。 */
  m_protocol.image_rx.parser.state = CAPSULE_PARSE_PREAMBLE;
  m_protocol.image_rx.parser.preamble_index = 0U;
  m_protocol.image_rx.parser.expected_length = 0U;
  m_protocol.image_rx.parser.received_length = 0U;
}

/** @brief 检查JPEG是否已经包含解码所需的量化表和哈夫曼表。 */
static uint8_t CapsuleProtocol_JpegHasTables(const uint8_t *jpeg,
                                             uint16_t length)
{
  /* 跳过开头SOI FF D8，从第一个JPEG段标记开始扫描。 */
  uint16_t position = 2U;
  uint8_t has_dqt = 0U;
  uint8_t has_dht = 0U;

  if ((length < 4U) || (jpeg[0] != 0xFFU) || (jpeg[1] != 0xD8U))
  {
    /* 连基本SOI都没有，不视为可解析JPEG。 */
    return 0U;
  }
  while ((uint16_t)(position + 1U) < length)
  {
    uint8_t marker;
    uint16_t segment_length;
    while ((position < length) && (jpeg[position] != 0xFFU))
    {
      /* 跳过当前位置前的非Marker数据。 */
      position++;
    }
    while ((position < length) && (jpeg[position] == 0xFFU))
    {
      /* JPEG允许连续多个0xFF填充字节，只取后面的Marker编号。 */
      position++;
    }
    if (position >= length)
    {
      break;
    }
    marker = jpeg[position++];
    if ((marker == 0xDAU) || (marker == 0xD9U))
    {
      /* SOS开始压缩数据，EOI结束图片；两者之后不再扫描表段。 */
      break;
    }
    if ((marker == 0xD8U) || (marker == 0x01U) ||
        ((marker >= 0xD0U) && (marker <= 0xD7U)))
    {
      /* SOI、TEM及RST0～RST7没有两字节段长度，直接继续。 */
      continue;
    }
    if ((uint16_t)(position + 1U) >= length)
    {
      break;
    }
    segment_length = (uint16_t)(((uint16_t)jpeg[position] << 8) |
                                jpeg[position + 1U]);
    /* JPEG段长度包含长度字段自身的2字节，必须完整落在输入范围内。 */
    if ((segment_length < 2U) ||
        ((uint32_t)position + segment_length > length))
    {
      break;
    }
    if (marker == 0xDBU)
    {
      /* FF DB：量化表DQT。 */
      has_dqt = 1U;
    }
    else if (marker == 0xC4U)
    {
      /* FF C4：哈夫曼表DHT。 */
      has_dht = 1U;
    }
    position = (uint16_t)(position + segment_length);
  }
  return (uint8_t)(has_dqt && has_dht);
}

/**
 * @brief 把nRF图片数据整理成可直接显示和保存的完整JPEG。
 *
 * 简易流程图：
 *   检查SOI FF D8
 *       → 扫描是否已有DQT和DHT
 *       ├─ 已有：原样复制JPEG
 *       └─ 缺少：标准JPEG头 + 原数据去掉旧SOI后拼接
 *       → 检查最终长度不超过19936字节记录容量
 */
static uint8_t CapsuleProtocol_BuildJpeg(const uint8_t *source,
                                         uint16_t source_length,
                                         uint16_t *jpeg_length,
                                         uint8_t *header_added)
{
  /* legacy_record前128字节留给DeviceInfo，JPEG从偏移128开始。 */
  uint8_t *destination = &m_protocol.record.data[CAPSULE_DEVICE_INFO_SIZE];

  /* 所有可接收图片至少应以JPEG SOI FF D8开头。 */
  if ((source_length < 4U) || (source[0] != 0xFFU) ||
      (source[1] != 0xD8U))
  {
    LOG_WARNING("Image rejected: missing JPEG SOI, length=%u",
                (unsigned int)source_length);
    return 0U;
  }
  //检查原图是否自带解码表
  if (CapsuleProtocol_JpegHasTables(source, source_length) != 0U)
  {
    /* 原图已经自带DQT/DHT，可直接按原长度复制。 */
    if (source_length > LEGACY_RECORD_JPEG_CAPACITY)
    {
      return 0U;
    }
    (void)memcpy(destination, source, source_length);
    *jpeg_length = source_length;
    *header_added = 0U;
  }
  else
  {
    /*
     * 胶囊原图缺少解码表：标准头已经包含SOI，因此拼接时跳过原图
     * 最前面的FF D8，避免出现两个连续SOI。
     */
    uint32_t rebuilt_length = LEGACY_JPEG_HEADER_SIZE + source_length - 2U;
    if (rebuilt_length > LEGACY_RECORD_JPEG_CAPACITY)
    {
      LOG_WARNING("Image rejected: rebuilt JPEG too large, raw=%u rebuilt=%lu max=%u",
                  (unsigned int)source_length, (unsigned long)rebuilt_length,
                  (unsigned int)LEGACY_RECORD_JPEG_CAPACITY);
      return 0U;
    }
    (void)memcpy(destination, g_legacy_jpeg_header,
                 LEGACY_JPEG_HEADER_SIZE);
    (void)memcpy(&destination[LEGACY_JPEG_HEADER_SIZE], &source[2],
                 source_length - 2U);
    *jpeg_length = (uint16_t)rebuilt_length;
    *header_added = 1U;
  }
  return 1U;
}

/**
 * @brief 更新128字节DeviceInfo中由STM负责的版本、ID、时间和JPEG长度字段。
 *
 * 简易流程图：
 *   保留RX已填的TX/RX/SN/天线/RSSI字段
 *       → 写STM版本和96位芯片唯一ID
 *       → 写运行秒数和最终JPEG长度
 *       → 尝试读取SD2058
 *       ├─ 成功：按旧协议顺序写入7字节RTC
 *       └─ 失败：保留接收帧中的RTC区域
 */
static void CapsuleProtocol_UpdateDeviceInfo(uint8_t *device_info,
                                             uint16_t jpeg_length)
{
  /* STM32F103唯一ID为连续12字节，只读取并复制，不修改系统区。 */
  const uint8_t *unique_id = (const uint8_t *)STM32_UNIQUE_ID_ADDRESS;
  /* 协议保存整秒运行时间，舍弃毫秒余数。 */
  uint32_t uptime_seconds = AppUartRx_GetMilliseconds() / 1000U;
  uint8_t rtc[7];

  /* 重写固定标识，确保最终记录头格式一致。 */
  device_info[0] = 0x00U;
  device_info[1] = 0x55U;
  device_info[2] = 0xAAU;
  device_info[3] = 0x88U;
  device_info[4] = 0x99U;
  /* 仅覆盖STM负责的版本和芯片ID字段，保留RX提供的SN/RSSI/天线信息。 */
  device_info[DEVICE_INFO_STM_VERSION_MAIN_OFFSET] = STM_VERSION_MAIN;
  device_info[DEVICE_INFO_STM_VERSION_MAIN_OFFSET + 1U] = STM_VERSION_SUB;
  device_info[DEVICE_INFO_STM_VERSION_TEST_OFFSET] = STM_VERSION_TEST;
  (void)memcpy(&device_info[DEVICE_INFO_STM_DEVICE_ID_OFFSET], unique_id, 12U);
  /* 运行秒数按小端顺序写入3字节，最大可表示0xFFFFFF秒。 */
  device_info[DEVICE_INFO_STM_UPTIME_OFFSET] = (uint8_t)uptime_seconds;
  device_info[DEVICE_INFO_STM_UPTIME_OFFSET + 1U] =
      (uint8_t)(uptime_seconds >> 8);
  device_info[DEVICE_INFO_STM_UPTIME_OFFSET + 2U] =
      (uint8_t)(uptime_seconds >> 16);
  /* 写入补头后的最终JPEG长度，同样使用3字节小端格式。 */
  device_info[DEVICE_INFO_JPEG_LENGTH_OFFSET] = (uint8_t)jpeg_length;
  device_info[DEVICE_INFO_JPEG_LENGTH_OFFSET + 1U] =
      (uint8_t)(jpeg_length >> 8);
  device_info[DEVICE_INFO_JPEG_LENGTH_OFFSET + 2U] =
      (uint8_t)(jpeg_length >> 16);

  /* 每张有效图片都重试RTC读取，使上电较慢的SD2058可以后续自动恢复。 */
  if (Sd2058_ReadTime(rtc) != 0U)
  {
    m_protocol.record.rtc_available = 1U;
    /* SD2058寄存器顺序与旧DeviceInfo协议顺序相反，按协议倒序填入。 */
    device_info[DEVICE_INFO_RTC_OFFSET + 6U] = rtc[0];
    device_info[DEVICE_INFO_RTC_OFFSET + 5U] = rtc[1];
    device_info[DEVICE_INFO_RTC_OFFSET + 4U] = (uint8_t)(rtc[2] & 0x7FU);
    device_info[DEVICE_INFO_RTC_OFFSET + 3U] = rtc[3];
    device_info[DEVICE_INFO_RTC_OFFSET + 2U] = rtc[4];
    device_info[DEVICE_INFO_RTC_OFFSET + 1U] = rtc[5];
    device_info[DEVICE_INFO_RTC_OFFSET] = rtc[6];
  }
  else
  {
    /* 读取失败时不清RTC区域，保留RX帧原有内容并标记当前不可用。 */
    m_protocol.record.rtc_available = 0U;
  }
}

/** @brief 根据设备信息中的8字节SN生成对应的.YS文件名。 */
static void CapsuleProtocol_BuildFilename(char filename[32],
                                          const uint8_t *device_info)
{
  /* DeviceInfo偏移11开始的8字节为胶囊SN。 */
  const uint8_t *sn = &device_info[DEVICE_INFO_CAPSULE_SN_OFFSET];
  /* 生成固定格式Y<16个大写十六进制字符>.YS。 */
  (void)snprintf(filename, 32U,
                 "Y%02X%02X%02X%02X%02X%02X%02X%02X.YS",
                 sn[0], sn[1], sn[2], sn[3],
                 sn[4], sn[5], sn[6], sn[7]);
}

/**
 * @brief 启动UART3 DMA，按ZAYS 0x81格式发送JPEG、130字节预留段和DeviceInfo。
 *
 * 简易流程图：
 *   检查PC发送状态必须IDLE
 *       → 生成7字节ZAYS/0x81头
 *       → 累加JPEG、130字节预留段和128字节DeviceInfo校验和
 *       → 保存长度并置PC_TX_HEADER
 *       → 启动第一段Header DMA
 *       → 后续各段由HAL_UART_TxCpltCallback继续推进
 */
static uint8_t CapsuleProtocol_ForwardToPc(const uint8_t *jpeg,
                                           uint16_t jpeg_length,
                                           const uint8_t *device_info)
{
  CapsulePcTxContext_t *tx = &m_protocol.pc_tx;
  uint8_t checksum = 0U;
  /* PC侧0x81 Payload：JPEG、130字节暂不解析信息、128字节DeviceInfo。 */
  uint16_t payload_length = (uint16_t)(jpeg_length +
      CAPSULE_PC_RESERVED_INFO_SIZE + CAPSULE_DEVICE_INFO_SIZE);
  uint16_t index;

  if (tx->state != PC_TX_IDLE)
  {
    /* 上一帧DMA尚未完成，调用者不能覆盖共享的legacy_record。 */
    return 0U;
  }
  /* 构造ZAYS + 命令0x81 + 两字节大端Payload长度。 */
  tx->header[0] = 0x5AU;
  tx->header[1] = 0x41U;
  tx->header[2] = 0x59U;
  tx->header[3] = 0x53U;
  tx->header[4] = CAPSULE_CMD_IMAGE_FORWARD;
  tx->header[5] = (uint8_t)(payload_length >> 8);
  tx->header[6] = (uint8_t)payload_length;
  for (index = 0U; index < jpeg_length; index++)
  {
    /* PC帧校验覆盖整个Payload，顺序与实际发送一致。 */
    checksum = (uint8_t)(checksum + jpeg[index]);
  }
  (void)memset(tx->reserved_info, 0x01, sizeof(tx->reserved_info));
  for (index = 0U; index < CAPSULE_PC_RESERVED_INFO_SIZE; index++)
  {
    checksum = (uint8_t)(checksum + tx->reserved_info[index]);
  }
  for (index = 0U; index < CAPSULE_DEVICE_INFO_SIZE; index++)
  {
    checksum = (uint8_t)(checksum + device_info[index]);
  }
  tx->checksum = checksum;
  tx->jpeg_length = jpeg_length;
  tx->state = PC_TX_HEADER;
  /* 这里只启动第一段；其余三段由HAL_UART_TxCpltCallback逐段推进。 */
  if (HAL_UART_Transmit_DMA(&huart3, tx->header,
                            sizeof(tx->header)) != HAL_OK)
  {
    /* 第一段都未能启动时立即释放状态，并延迟到主循环报告错误。 */
    tx->state = PC_TX_IDLE;
    tx->error_events++;
    return 0U;
  }
  return 1U;
}

/**
 * @brief 校验完整nRF图片帧，生成JPEG、写入SD卡并转发PC。
 *
 * 简易流程图：
 *   检查payload长度和JPEG原始长度
 *       → 校验DeviceInfo+JPEG累加和
 *       → 校验DeviceInfo固定标识
 *       → 检查上一帧PC DMA是否结束
 *       → 清空20064B记录并复制128B DeviceInfo
 *       → 原JPEG完整则复制，否则补DQT/DHT标准头
 *       → 补STM版本/ID/运行时间/RTC/JPEG长度
 *       ├─ 启动USART3 DMA转发PC
 *       └─ 追加固定20064B记录到Y<SN>.YS（每10帧sync）
 */
static void CapsuleProtocol_HandleFrame(void)
{
  CapsuleParser_t *parser = &m_protocol.image_rx.parser;    			//当前图片帧解析状态和统计信息
  uint8_t *frame = m_protocol.image_rx.frame;                           //从 NRF 收到的原始完整帧
  CapsuleRecordContext_t *record = &m_protocol.record;                  //整理后用于 PC 发送和 SD 保存的固定长度记录
  uint16_t source_length;         			//NRF 原始传来的 JPEG 数据长度
  uint16_t jpeg_length;           			//- 经过 CapsuleProtocol_BuildJpeg() 整理后的实际 JPEG 长度。
  uint16_t index;
  uint8_t checksum = 0U;
  uint8_t received_checksum;
  uint8_t header_added;                 	//表示 BuildJpeg() 是否补加了 JPEG 头：- 0：原始图片已有完整头；- 非 0：本机补加了头。
  uint8_t sync_now;                     	//表示本次 SD 写入是否要求执行同步操作。
  uint32_t frame_arrival_ms;             	//记录合法帧到达本机的时间
  char filename[32];                     	//保存生成的 SD 文件名
  const uint8_t *source;                    //指向原始帧中的 JPEG 数据
  uint8_t *device_info = record->data;      //device_info直接指向记录起始地址
  SdStorageStatus_t storage_status;         //保存 SD 写入结果

  /* Payload至少必须包含完整的128字节DeviceInfo。 */
  if (parser->expected_length < CAPSULE_DEVICE_INFO_SIZE)
  {
    parser->rejected_frames++;
    return;
  }
  /* 原始JPEG长度 = Payload长度 - DeviceInfo长度。 */
  source_length = (uint16_t)(parser->expected_length -
                             CAPSULE_DEVICE_INFO_SIZE);
  if ((source_length == 0U) || (source_length > CAPSULE_IMAGE_MAX_SIZE))
  {
    parser->rejected_frames++;
    return;
  }
  /* 图片源紧跟在7字节Header和128字节DeviceInfo之后。 */
  source = &frame[CAPSULE_FRAME_HEADER_SIZE + CAPSULE_DEVICE_INFO_SIZE];
  /* RX发送端对完整payload求和：128字节DeviceInfo + JPEG。 */
  for (index = 0U; index < parser->expected_length; index++)
  {
    checksum = (uint8_t)(checksum +
        frame[CAPSULE_FRAME_HEADER_SIZE + index]);
  }
  /* Checksum位于Payload最后一个字节之后。 */
  received_checksum = frame[CAPSULE_FRAME_HEADER_SIZE +
                            parser->expected_length];
  if (checksum != received_checksum)
  {
    parser->rejected_frames++;
    LOG_WARNING("Frame checksum failed: calculated=0x%02X received=0x%02X",
                checksum, received_checksum);
    return;
  }
  
  //校验 DeviceInfo 标志
  if (CapsuleProtocol_DeviceInfoIsValid(
          &frame[CAPSULE_FRAME_HEADER_SIZE]) == 0U)
  {
    parser->rejected_frames++;
    LOG_WARNING("Frame rejected: invalid 128-byte DeviceInfo marker");
    return;
  }
  /* 在SD写入和USART1日志之前取时间，避免把本机处理耗时算进无线帧间隔。 */
  frame_arrival_ms = AppUartRx_GetMilliseconds();

  /* legacy_record在USART3图片DMA结束前一直是发送源，不能被下一帧覆盖。 */
  if (m_protocol.pc_tx.state != PC_TX_IDLE)
  {
    parser->rejected_frames++;
    LOG_WARNING("Complete frame dropped: previous PC DMA transfer still busy");
    return;
  }

  /* 固定记录未使用区域全部补0，保证每条.YS记录恒为20064字节。 */
  (void)memset(record->data, 0, sizeof(record->data));
  /* 先保留RX传来的完整DeviceInfo，之后只补写STM负责的字段。 */
  (void)memcpy(device_info, &frame[CAPSULE_FRAME_HEADER_SIZE],
               CAPSULE_DEVICE_INFO_SIZE);
  if (CapsuleProtocol_BuildJpeg(source, source_length, &jpeg_length,
                                &header_added) == 0U)
  {
    parser->rejected_frames++;
    return;
  }
  CapsuleProtocol_UpdateDeviceInfo(device_info, jpeg_length);      //更新 DeviceInfo     补充或修改 STM32 负责的字段
  CapsuleProtocol_LogFrameTiming(frame_arrival_ms, source_length,  //输出帧时间日志
                                 jpeg_length);
  CapsuleProtocol_BuildFilename(filename, device_info);            //生成文件名

  /* 尽早启动USART3 DMA，让PC发送与DeviceInfo日志、SD写入并行。 */
  if (CapsuleProtocol_ForwardToPc(&record->data[CAPSULE_DEVICE_INFO_SIZE],
                                  jpeg_length, device_info) != 0U)
  {
    LOG_INFO("PC forwarding queued: jpeg=%u unparsed=130 device_info=128 payload=%u",
             (unsigned int)jpeg_length,
             (unsigned int)(jpeg_length + CAPSULE_PC_RESERVED_INFO_SIZE +
                            CAPSULE_DEVICE_INFO_SIZE));
  }
  CapsuleProtocol_LogDeviceInfo(device_info);

  /*
 * 每成功写入10条记录执行一次f_sync：
 * 前9条仅调用f_write，第10条在f_write成功后调用f_sync。
 */
  sync_now = (record->records_since_sync >= 9U) ? 1U : 0U;
  storage_status = SdStorage_AppendCapsuleRecord(filename, record->data,
                                                 sizeof(record->data),
                                                 sync_now);
  if (storage_status == SD_STORAGE_OK)
  {
    /* sync成功后重新计数，否则继续累计到下一次第10帧。 */
    parser->stored_frames++;
    record->records_since_sync = sync_now ? 0U :
        (uint8_t)(record->records_since_sync + 1U);
    LOG_INFO("YS stored: file=%s record=%lu jpeg=%u raw=%u header=%s sync=%s",
             filename, (unsigned long)parser->stored_frames,
             (unsigned int)jpeg_length, (unsigned int)source_length,
             header_added ? "added" : "native",
             sync_now ? "yes" : "no");
  }
  else
  {
    LOG_ERROR("YS storage failed: file=%s status=%u", filename,
              (unsigned int)storage_status);
  }

  parser->completed_frames++;
}

/**
 * @brief 初始化STM32协议解析、SD记录计数、RTC和PC异步发送状态。
 *
 * 简易流程图：
 *   清零图片解析器/统计量
 *       → PC DMA状态置IDLE
 *       → 清空nRF和PC控制帧拼包状态
 *       → 图片解析器回到PREAMBLE
 *       → 初始化SD2058并探测一次时间
 */
void CapsuleProtocol_Init(void)
{
  /*
   * 只清运行状态，不重复清零约40KB的图片/记录大数组。
   * 静态数组上电时已由C运行库初始化，后续也会按实际帧长度覆盖。
   */
  (void)memset(&m_protocol.image_rx.parser, 0,
               sizeof(m_protocol.image_rx.parser));
  (void)memset(&m_protocol.nrf_control, 0,
               sizeof(m_protocol.nrf_control));
  (void)memset(&m_protocol.pc_control, 0,
               sizeof(m_protocol.pc_control));
  (void)memset(&m_protocol.pc_tx, 0,
               sizeof(m_protocol.pc_tx));
  (void)memset(&m_protocol.stats, 0,
               sizeof(m_protocol.stats));
  m_protocol.record.records_since_sync = 0U;
  m_protocol.record.rtc_available = 0U;
  m_protocol.pc_tx.state = PC_TX_IDLE;
  /* 明确进入图片前导码搜索状态。 */
  CapsuleProtocol_ResetParser();
  /* RTC失败不阻止系统启动；后续每张完整图片都会再次尝试读取。 */
  m_protocol.record.rtc_available = Sd2058_Init();
  if (m_protocol.record.rtc_available != 0U)
  {
    LOG_INFO("SD2058 ready: software I2C PB6=SCL PB7=SDA");
  }
  else
  {
    LOG_WARNING("SD2058 unavailable; RTC fields remain from received DeviceInfo");
  }
}

/**
 * @brief 主循环服务：汇总DMA事件，并在USART3空闲时发送等待中的控制应答。
 *
 * 简易流程图：
 *   短临界区取走DMA完成/错误计数
 *       → 主循环打印完成或错误日志
 *       → 有pending控制应答且图片DMA空闲？
 *       ├─ 是：阻塞发送短控制帧，成功后清pending
 *       └─ 否：等待下一轮
 */
void CapsuleProtocol_Process(void)
{
	/*
	tx是 PC 图片发送上下文，保存：
	- 当前图片 DMA 状态；
	- DMA 完成事件数；
	- DMA 错误事件数；
	- 累计完成次数。
	*/
  CapsulePcTxContext_t *tx = &m_protocol.pc_tx;
	/*
	control是 NRF 控制应答上下文，保存：
	- 等待发给 PC 的控制应答；
	- 控制应答长度；
	- 控制帧解析状态。
	*/
  CapsuleNrfControlContext_t *control = &m_protocol.nrf_control;
  uint32_t completed;								//本轮取出的DMA完成事件数量
  uint32_t completed_total;							//系统启动以来累计完成数量
  uint32_t errors;                                  //本轮取出的DMA错误事件数量
  uint32_t interrupt_state = __get_PRIMASK();       //保存进入函数前的中断状态

  /*
   * 完成/错误计数由USART3 DMA中断更新；短临界区内取快照并清零，
   * 防止主循环读取过程中丢失新事件。
   解决了三个问题：
	1. 保证“读取事件计数并清零”不可被 DMA 中断打断，防止新事件被清零覆盖。
	2. 临界区只做几次整数读写，保持中断关闭时间非常短。
	3. 保存并恢复原来的中断状态，避免破坏调用者已有的外层临界区。
   */
  __disable_irq();
  completed = tx->completed_events;
  completed_total = tx->completed_total;
  errors = tx->error_events;
  tx->completed_events = 0U;
  tx->error_events = 0U;
  if (interrupt_state == 0U)
  {
    __enable_irq();
  }
  
  
  if (completed != 0U)
  {
    LOG_INFO("PC forwarding completed: events=%lu total=%lu",
             (unsigned long)completed,
             (unsigned long)completed_total);
  }
  if (errors != 0U)
  {
    LOG_ERROR("PC forwarding DMA failed: count=%lu", (unsigned long)errors);
  }
  if ((control->pending_length != 0U) &&
      (tx->state == PC_TX_IDLE))
  {
    /* 图片DMA空闲后，才允许短控制应答占用同一个USART3。 */
    uint16_t pending_length = control->pending_length;
    if (HAL_UART_Transmit(&huart3, control->pending_frame, pending_length,
                          CAPSULE_UART_TIMEOUT_MS) == HAL_OK)
    {
      if (control->pending_frame[4] == 0x05U)
      {
        LOG_INFO("[DISCOVERY] SN forwarded to PC");
      }
      /* 只有确认发送成功才清pending；失败时保留，下一轮继续重试。 */
      control->pending_length = 0U;
    }
    else
    {
      LOG_ERROR("USART3 control response forwarding failed");
    }
  }
}

/**
 * @brief UART3 DMA完成回调，推进一帧图片的五段式异步发送。
 *
 * 简易流程图：
 *   HEADER完成 → 启动JPEG DMA
 *       → JPEG完成 → 启动130B预留段DMA → 启动128B DeviceInfo DMA
 *       → DeviceInfo完成 → 启动1B checksum DMA
 *       → checksum完成 → 状态回IDLE并累计完成事件
 *       → 任一步启动失败 → 回IDLE并累计错误事件
 *
 * @note 中断回调只推进DMA和计数，不打印日志、不写SD卡。
 */
void HAL_UART_TxCpltCallback(UART_HandleTypeDef *huart)
{
  CapsulePcTxContext_t *tx = &m_protocol.pc_tx;
  HAL_StatusTypeDef result = HAL_OK;

  /* USART1/2的发送完成事件不属于PC图片DMA状态机。 */
  if (huart->Instance != USART3)
  {
    return;
  }
  if (tx->state == PC_TX_HEADER)
  {
    /* 第一段Header完成，启动第二段JPEG。 */
    tx->state = PC_TX_JPEG;
    result = HAL_UART_Transmit_DMA(&huart3,
        &m_protocol.record.data[CAPSULE_DEVICE_INFO_SIZE], tx->jpeg_length);
  }
  else if (tx->state == PC_TX_JPEG)
  {
    /* 第二段JPEG完成，启动第三段130字节预留信息。 */
    tx->state = PC_TX_RESERVED_INFO;
    result = HAL_UART_Transmit_DMA(&huart3, tx->reserved_info,
                                   CAPSULE_PC_RESERVED_INFO_SIZE);
  }
  else if (tx->state == PC_TX_RESERVED_INFO)
  {
    /* 第三段预留信息完成，启动第四段DeviceInfo。 */
    tx->state = PC_TX_DEVICE_INFO;
    result = HAL_UART_Transmit_DMA(&huart3, m_protocol.record.data,
                                   CAPSULE_DEVICE_INFO_SIZE);
  }
  else if (tx->state == PC_TX_DEVICE_INFO)
  {
    /* 第四段DeviceInfo完成，启动第五段单字节Checksum。 */
    tx->state = PC_TX_CHECKSUM;
    result = HAL_UART_Transmit_DMA(&huart3, &tx->checksum, 1U);
  }
  else if (tx->state == PC_TX_CHECKSUM)
  {
    /* 第五段完成才代表整帧已离开USART3，允许复用legacy_record。 */
    tx->state = PC_TX_IDLE;
    tx->completed_events++;
    tx->completed_total++;
  }
  else
  {
    return;
  }

  if (result != HAL_OK)
  {
    /* 任一后续DMA段启动失败都终止本帧，并由主循环统一打印错误。 */
    tx->state = PC_TX_IDLE;
    tx->error_events++;
  }
}

/**
 * @brief 接收USART2字节流，区分ZAYS控制应答和FF551234图片帧。
 *
 * 简易流程图：
 *   对输入块逐字节处理
 *       → PREAMBLE状态先尝试ZAYS控制帧解析
 *       → 非控制字节匹配FF 55 12 34 81
 *       → 读取2字节payload长度并检查128～20128范围
 *       → 接收payload和末尾checksum
 *       → 完整后HandleFrame，再复位解析器等待下一帧
 *
 * @note 支持任意DMA分段边界；一次调用可以只收到半个帧头或部分JPEG。
 */
void CapsuleProtocol_InputFromNrf(const uint8_t *data, uint16_t length)
{
  CapsuleParser_t *parser = &m_protocol.image_rx.parser;
  uint8_t *frame = m_protocol.image_rx.frame;
  uint16_t input_index;

  /* 空指针或空数据块没有可解析内容，直接返回。 */
  if ((data == 0) || (length == 0U))
  {
    return;
  }

  /*
   * 流式逐字节解析：data不要求包含完整帧。
   * capsule_parser和capsule_frame是静态全局对象，因此解析状态可以跨越
   * 多次DMA/ring数据块调用继续保持。
   */
  for (input_index = 0U; input_index < length; input_index++)
  {
    uint8_t byte = data[input_index];

    /*
     * 只有尚未进入图片帧时，才允许识别ZAYS控制应答。
     * 返回非0表示当前字节已被控制帧解析器消费，不能再交给图片解析器；
     * 已进入LENGTH/BODY后不再检测控制头，避免JPEG中的5A 41 59 53被误判。
     */
    if ((parser->state == CAPSULE_PARSE_PREAMBLE) &&
        (CapsuleProtocol_InputControlByte(byte) != 0U))
    {
		//跳过本次循环剩余部分
		continue;
    }

    /* 状态一：逐字节寻找固定图片帧头 FF 55 12 34 81。 */
    if (parser->state == CAPSULE_PARSE_PREAMBLE)
    {
      if (byte == capsule_preamble[parser->preamble_index])
      {
        /* 当前字节符合预期，保存帧头并推进匹配位置。 */
        frame[parser->preamble_index] = byte;
        parser->preamble_index++;
        if (parser->preamble_index == CAPSULE_FRAME_PREAMBLE_SIZE)
        {
          /* 5字节帧头完整，接下来读取2字节大端Payload长度。 */
          parser->state = CAPSULE_PARSE_LENGTH;
          parser->received_length = CAPSULE_FRAME_PREAMBLE_SIZE;
        }
      }
      else
      {
        /*
         * 帧头中途失配：如果当前字节本身又是0xFF，就把它当作
         * 下一次帧头的新起点；否则完全清空帧头匹配进度。
         */
        parser->preamble_index = (byte == capsule_preamble[0]) ? 1U : 0U;
        if (parser->preamble_index != 0U)
        {
          frame[0] = byte;
        }
      }
    }

    /* 状态二：帧头已确认，继续收下标5、6处的2字节Payload长度。 */
    else if (parser->state == CAPSULE_PARSE_LENGTH)
    {
      frame[parser->received_length++] = byte;
      if (parser->received_length == CAPSULE_FRAME_HEADER_SIZE)
      {
        /* 长度字段为大端格式：frame[5]是高字节，frame[6]是低字节。 */
        parser->expected_length =
            (uint16_t)(((uint16_t)frame[5] << 8) | frame[6]);

        /*
         * Payload必须至少容纳128字节DeviceInfo，且JPEG不能超过20000字节。
         * 提前拒绝异常长度，防止后续写出capsule_frame数组边界。
         */
        if ((parser->expected_length < CAPSULE_DEVICE_INFO_SIZE) ||
            (parser->expected_length >
             (CAPSULE_DEVICE_INFO_SIZE + CAPSULE_IMAGE_MAX_SIZE)))
        {
          parser->rejected_frames++;
          LOG_WARNING("Frame length rejected: %u",
                      (unsigned int)parser->expected_length);
          CapsuleProtocol_ResetParser();
        }
        else
        {
          /* 长度合法，开始接收DeviceInfo、JPEG以及末尾Checksum。 */
          parser->state = CAPSULE_PARSE_BODY;
        }
      }
    }
    else
    {
      /*
       * 状态三（BODY）：依次保存Payload和最后1字节Checksum。
       * Payload布局为128字节DeviceInfo + JPEG数据。
       */
      frame[parser->received_length++] = byte;

      /* 完整帧总长度 = 7字节Header + Payload长度 + 1字节Checksum。 */
      if (parser->received_length ==
          (uint16_t)(CAPSULE_FRAME_HEADER_SIZE +
                     parser->expected_length + 1U))
      {
        /* 完整帧已收齐：校验、补充信息、转发PC并保存SD。 */
        CapsuleProtocol_HandleFrame();

        /* 无论完整帧处理成功或被拒绝，都重新寻找下一条图片帧头。 */
        CapsuleProtocol_ResetParser();
      }
    }
  }
}

/**
 * @brief 接收PC数据；完整0x02命令本地设置RTC，其余内容转发nRF RX。
 *
 * 简易流程图：
 *   对USART3输入逐字节匹配ZAYS
 *       ├─ 不是ZAYS：原字节/已匹配片段透明发USART2
 *       └─ 是ZAYS：按长度拼完整控制帧并校验
 *             ├─ CMD=0x02且7字节BCD合法：写SD2058，不转发nRF
 *             └─ 其他CMD：完整帧阻塞转发USART2
 *
 * @note PC控制帧最大32字节；USART2阻塞发送超时为1000ms。
 */
void CapsuleProtocol_InputFromPc(const uint8_t *data, uint16_t length)
{
  /* PC控制帧统一使用ZAYS四字节同步头。 */
  static const uint8_t header[4] = {0x5AU, 0x41U, 0x59U, 0x53U};
  CapsuleControlParser_t *parser = &m_protocol.pc_control;
  uint16_t index;

  /* 空输入不改变当前跨数据块保存的PC控制帧匹配状态。 */
  if ((data == 0) || (length == 0U))
  {
    return;
  }
  for (index = 0U; index < length; ++index)
  {
    uint8_t byte = data[index];

    /* 第一阶段：尝试逐字节匹配ZAYS；未确认前不能确定是不是控制帧。 */
    if (parser->received < 4U)
    {
      if (byte == header[parser->received])
      {
        /* 当前同步头字节匹配，先缓存，等待后续字节确认完整ZAYS。 */
        parser->frame[parser->received++] = byte;
        continue;
      }
      if (parser->received != 0U)
      {
        /*
         * 已匹配的ZAYS前缀中途失配，说明它只是普通透明数据；
         * 先把缓存前缀原样发给nRF，再单独处理当前失配字节。
         */
        (void)HAL_UART_Transmit(&huart2, parser->frame,
                               parser->received,
                               CAPSULE_UART_TIMEOUT_MS);
        parser->received = 0U;
      }
      if (byte == header[0])
      {
        /* 当前字节0x5A可能是下一条ZAYS的新起点，暂不立即转发。 */
        parser->frame[parser->received++] = byte;
      }
      else
      {
        /* 与ZAYS无关的普通字节直接透明转发给nRF RX。 */
        (void)HAL_UART_Transmit(&huart2, &byte, 1U,
                               CAPSULE_UART_TIMEOUT_MS);
      }
      continue;
    }
	
    if (parser->received >= sizeof(parser->frame))
    {
      /* 防御性复位，禁止异常状态继续写出32字节控制缓存边界。 */
      parser->received = 0U;
      parser->expected = 0U;
      continue;
    }
    /* 第二阶段：ZAYS已确认，继续接收命令、长度、Payload和Checksum。 */
    parser->frame[parser->received++] = byte;
    if (parser->received == 7U)
    {
      /* 完整帧长度 = 固定8字节开销 + Payload长度。 */
      parser->expected = (uint16_t)(8U +
          (((uint16_t)parser->frame[5] << 8) | parser->frame[6]));
      if (parser->expected > sizeof(parser->frame))
      {
        /*
         * 长度超过本地控制缓存：这不是本模块能处理的短控制帧，
         * 将已收到的7字节原样交给nRF，然后重新开始同步。
         */
        (void)HAL_UART_Transmit(&huart2, parser->frame,
                               parser->received,
                               CAPSULE_UART_TIMEOUT_MS);
        parser->received = 0U;
        parser->expected = 0U;
      }
    }
    if ((parser->expected != 0U) &&
        (parser->received == parser->expected))
    {
      /* 收到声明长度的完整控制帧后，校验Payload的8位累加和。 */
      uint16_t payload_length = (uint16_t)(parser->expected - 8U);
      uint8_t valid = (CapsuleProtocol_ControlChecksum(&parser->frame[7],
          payload_length) == parser->frame[parser->expected - 1U]);
      if (parser->frame[4] == 0x02U)
      {
        /* 0x02由STM本地消费：7字节时间数据合法时写入SD2058，不转发nRF。 */
        if ((payload_length == 7U) && (valid != 0U) &&
            (Sd2058_WriteTime(&parser->frame[7]) != 0U))
        {
          m_protocol.record.rtc_available = 1U;
          LOG_INFO("RTC updated from PC command 0x02");
        }
        else
        {
          LOG_ERROR("RTC update from PC command 0x02 failed");
        }
      }
      else
      {
        /* 除0x02外的完整控制命令均保持原帧格式阻塞转发USART2。 */
        if (HAL_UART_Transmit(&huart2, parser->frame,
                              parser->expected,
                              CAPSULE_UART_TIMEOUT_MS) != HAL_OK)
        {
          LOG_ERROR("USART2 command forwarding failed");
        }
      }
      /* 一帧处理结束，无论成功失败都回到寻找下一条ZAYS同步头。 */
      parser->received = 0U;
      parser->expected = 0U;
    }
  }
}
