"""
gen_protocol_xlsx.py
生成 TYGD31 协议速查表 Excel 文档
输出: docs/TYGD31_PROTOCOL.xlsx
"""

import xlsxwriter
import os

OUT_PATH = os.path.join(os.path.dirname(__file__), "..", "docs", "TYGD31_PROTOCOL.xlsx")
OUT_PATH = os.path.abspath(OUT_PATH)
os.makedirs(os.path.dirname(OUT_PATH), exist_ok=True)

wb = xlsxwriter.Workbook(OUT_PATH)

# ----------------------------------------------------------------------
# 共用样式
# ----------------------------------------------------------------------
title_fmt = wb.add_format({
    "bold": True, "font_size": 16, "align": "center",
    "valign": "vcenter", "bg_color": "#1F4E78", "font_color": "white",
    "border": 2,
})
h1 = wb.add_format({
    "bold": True, "font_size": 12, "bg_color": "#305496",
    "font_color": "white", "align": "left", "valign": "vcenter",
    "border": 1,
})
h2 = wb.add_format({
    "bold": True, "font_size": 11, "bg_color": "#8EA9DB",
    "font_color": "black", "align": "left", "valign": "vcenter",
    "border": 1,
})
cell = wb.add_format({
    "font_size": 10, "align": "left", "valign": "vcenter",
    "border": 1, "text_wrap": True,
})
cell_c = wb.add_format({
    "font_size": 10, "align": "center", "valign": "vcenter",
    "border": 1, "text_wrap": True,
})
mono = wb.add_format({
    "font_size": 10, "font_name": "Consolas", "align": "left",
    "valign": "vcenter", "border": 1, "text_wrap": True,
})
mono_c = wb.add_format({
    "font_size": 10, "font_name": "Consolas", "align": "center",
    "valign": "vcenter", "border": 1,
})
diff_old = wb.add_format({
    "font_size": 10, "bg_color": "#FFE4B5", "border": 1, "text_wrap": True,
    "align": "left", "valign": "vcenter",
})
diff_new = wb.add_format({
    "font_size": 10, "bg_color": "#C6EFCE", "border": 1, "text_wrap": True,
    "align": "left", "valign": "vcenter",
})
diff_same = wb.add_format({
    "font_size": 10, "bg_color": "#D9E1F2", "border": 1, "text_wrap": True,
    "align": "left", "valign": "vcenter",
})
note = wb.add_format({
    "font_size": 9, "italic": True, "font_color": "#666666",
    "text_wrap": True, "align": "left", "valign": "top",
})


def write_sheet(name, columns, rows, col_widths=None, row_height=None,
                freeze=(1, 1), header_rows=1):
    """通用写入函数：name=表名, columns=表头列表, rows=数据二维列表"""
    ws = wb.add_worksheet(name)
    ws.merge_range(0, 0, 0, len(columns) - 1,
                   f"TYGD31 协议 — {name}", title_fmt)
    ws.set_row(0, 28)

    # 表头
    for c, h in enumerate(columns):
        ws.write(header_rows, c, h, h1)
    ws.set_row(header_rows, 22)

    # 数据
    for r, row in enumerate(rows):
        for c, val in enumerate(row):
            if isinstance(val, dict):
                fmt = val.get("fmt", cell)
                text = val.get("text", "")
            else:
                fmt = cell
                text = val
            ws.write(header_rows + 1 + r, c, text, fmt)

    # 列宽
    if col_widths:
        for c, w in enumerate(col_widths):
            ws.set_column(c, c, w)
    # 行高
    if row_height:
        ws.set_row(header_rows + 1, row_height)
    ws.freeze_panes(*freeze)
    return ws


# ============================================================
# Sheet 1: 协议总览
# ============================================================
ws1 = wb.add_worksheet("00_总览")
ws1.merge_range(0, 0, 0, 3, "TYGD31 胶囊系统 协议总览", title_fmt)
ws1.set_row(0, 28)
overview = [
    ["系统角色", "胶囊发送板 / RF 接收板 / STM32 主控 / PC 上位机"],
    ["RF 频率", "2400 MHz（NRF_RADIO->FREQUENCY = 0）"],
    ["RF 速率", "Nordic 2 Mbit"],
    ["RF 包长", "254 字节固定"],
    ["RF 地址", "PREFIX0=0xC4C3C2E7, PREFIX1=0xC5C6C7C8, BASE0=0xE7E7E7E7, BASE1=0x00C2C2C2"],
    ["RF CRC", "16 位, CRCINIT=0xFFFF, CRCPOLY=0x11021"],
    ["UART3", "1 Mbps, 8N1, TX=P0.15, RX=P0.16"],
    ["UART3 帧头", "FF 55 12 34 cmd lenHI lenLO (7 字节) + N 字节 data + 1 字节 CRC8"],
    ["SD 文件", "Y<7字节 SN>.YS, 每帧 20064 字节"],
    ["图像", "JPEG (FF D8 ... FF D9), header 686 + data ≤ 19250"],
    ["胶囊 SN 长度", "8 字节（来自 FICR DEVICEID 或 Flash 设定）"],
    ["DeviceInfo", "128 字节（含版本号/SN/RTC 时间等）"],
]
ws1.set_column(0, 0, 24)
ws1.set_column(1, 1, 80)
ws1.set_column(2, 2, 24)
ws1.set_column(3, 3, 24)
r = 1
for k, v in overview:
    ws1.write(r, 0, k, h2)
    ws1.write(r, 1, v, cell)
    r += 1


# ============================================================
# Sheet 2: Radio 命令字
# ============================================================
cmd_rows = [
    ["0x01", "CMD_IMG_BEGIN", "LEGACY_CMD_IMAGE_BEGIN",
     "图像开始包", "TX→RX", "cmd_byte, frame_id, capsule_sn, block_size, fragment_count, VERSION_*"],
    ["0x03", "CMD_IMG_END", "LEGACY_CMD_IMAGE_END",
     "图像结束包", "TX→RX", "cmd_byte, frame_id, capsule_sn, checksum"],
    ["0x05", "CMD_CAPSULE_SN_BROADCAST", "LEGACY_CMD_CAPSULE_SN_BROADCAST",
     "SN 广播", "TX→RX→STM→PC", "cmd_byte + 8 字节 SN"],
    ["0x07", "CMD_CAPSULE_ID_BROADCAST", "LEGACY_CMD_CAPSULE_ID_BROADCAST",
     "ID 广播（出厂 DEVICEID）", "TX→RX→STM→PC", "cmd_byte + 8 字节 ID"],
    ["0x10", "CMD_IMG_RCVD_RSP", "LEGACY_CMD_IMAGE_RECEIVED_RESPONSE",
     "图像接收应答", "RX→TX", "cmd_byte, frame_id, capsule_sn"],
    ["0x80", "CMD_IMG_DATA_PACKET", "LEGACY_CMD_IMAGE_DATA",
     "图像数据包", "TX→RX", "cmd_byte, frame_id, capsule_sn, fragment_index, JPEG payload"],
    ["0x81", "CMD_IMG_FORWARD", "LEGACY_CMD_IMAGE_FORWARD",
     "图像转发（用于 UART3）", "RX→STM→PC", "cmd_byte + JPEG 数据 + DeviceInfo"],
]
write_sheet(
    "01_Radio_命令字",
    ["HEX", "原 TYGD31 名称", "当前实现名称", "含义", "方向", "payload 字段"],
    cmd_rows,
    col_widths=[10, 32, 36, 22, 18, 56],
)


# ============================================================
# Sheet 3: BEGIN 包字段布局
# ============================================================
begin_rows = [
    ["0", "1", "cmd", "0x01", "CMD_IMG_BEGIN_PACKET", "LEGACY_CMD_IMAGE_BEGIN", "相同"],
    ["1", "1", "frame_id", "0~255", "image_id++", "frame_id++ (0~255 循环)", "相同"],
    ["2~9", "8", "capsule_sn", "8 字节", "capsule_sn_out[8]", "g_capsule_sn[8]", "相同"],
    ["10~11", "2", "block_size (大端)", "0~65535", "img_data_len", "block_size", "相同"],
    ["12~13", "2", "fragment_count (大端)", "0~65535", "img_data_pkt_num", "fragment_count", "相同"],
    ["14", "1", "VERSION_MAIN", "0~255", "VERSION_MAIN", "VERSION_MAIN", "相同"],
    ["15", "1", "VERSION_SUB", "0~255", "VERSION_SUB", "VERSION_SUB", "相同"],
    ["16", "1", "VERSION_TEST", "0~255", "VERSION_TEST", "VERSION_TEST", "相同"],
    ["17~253", "237", "保留", "0x00", "未使用", "未使用", "相同"],
]
write_sheet(
    "02_BEGIN_包布局",
    ["偏移", "长度", "字段", "取值", "原 TYGD31 写入", "当前实现写入", "对比"],
    begin_rows,
    col_widths=[10, 10, 22, 12, 28, 28, 12],
)


# ============================================================
# Sheet 4: DATA 包字段布局
# ============================================================
data_rows = [
    ["0", "1", "cmd", "0x80", "CMD_IMG_DATA_PACKET", "LEGACY_CMD_IMAGE_DATA", "相同"],
    ["1", "1", "frame_id", "0~255", "image_id", "frame_id", "相同"],
    ["2~9", "8", "capsule_sn", "8 字节", "capsule_sn_out[8]", "g_capsule_sn[8]", "相同"],
    ["10~11", "2", "fragment_index (大端)", "0~65535", "i/256, i%256", "LegacyProtocol_PutU16Be", "相同"],
    ["12~253", "≤242", "JPEG 载荷", "JPEG 数据", "memcpy from image_buff",
     "cx93510_frame_buffer_read", "相同"],
]
write_sheet(
    "03_DATA_包布局",
    ["偏移", "长度", "字段", "取值", "原 TYGD31 写入", "当前实现写入", "对比"],
    data_rows,
    col_widths=[10, 10, 22, 12, 28, 28, 12],
)


# ============================================================
# Sheet 5: END 包字段布局
# ============================================================
end_rows = [
    ["0", "1", "cmd", "0x03", "CMD_IMG_END_PACKET", "LEGACY_CMD_IMAGE_END", "相同"],
    ["1", "1", "frame_id", "0~255", "image_id", "frame_id", "相同"],
    ["2~9", "8", "capsule_sn", "8 字节", "capsule_sn_out[8]", "g_capsule_sn[8]", "相同"],
    ["10", "1", "checksum (8 位累加)", "0~255", "image_data_check_sum", "legacy_checksum", "相同"],
    ["11~253", "243", "保留", "0x00", "未使用", "未使用", "相同"],
]
write_sheet(
    "04_END_包布局",
    ["偏移", "长度", "字段", "取值", "原 TYGD31 写入", "当前实现写入", "对比"],
    end_rows,
    col_widths=[10, 10, 22, 12, 28, 28, 12],
)


# ============================================================
# Sheet 6: ACK 包字段布局
# ============================================================
ack_rows = [
    ["0", "1", "cmd", "0x10", "CMD_IMG_RCVD_RSP", "LEGACY_CMD_IMAGE_RECEIVED_RESPONSE", "相同"],
    ["1", "1", "frame_id", "0~255", "image_id", "frame_id", "相同"],
    ["2~9", "8", "capsule_sn", "8 字节", "capsule_sn_out[8]", "g_capsule_sn[8]", "相同"],
    ["10~253", "244", "保留", "0x00", "未使用", "未使用", "相同"],
]
write_sheet(
    "05_ACK_包布局",
    ["偏移", "长度", "字段", "取值", "原 TYGD31 写入", "当前实现写入", "对比"],
    ack_rows,
    col_widths=[10, 10, 22, 12, 28, 28, 12],
)


# ============================================================
# Sheet 7: UART3 帧布局
# ============================================================
uart3_rows = [
    ["0", "1", "帧头标记 1", "0xFF", "Comm_protocol_head[0]", "FRAME_HEADER_BYTES[0]", "相同"],
    ["1", "1", "帧头标记 2", "0x55", "Comm_protocol_head[1]", "FRAME_HEADER_BYTES[1]", "相同"],
    ["2", "1", "帧头标记 3", "0x12", "Comm_protocol_head[2]", "FRAME_HEADER_BYTES[2]", "相同"],
    ["3", "1", "帧头标记 4", "0x34", "Comm_protocol_head[3]", "FRAME_HEADER_BYTES[3]", "相同"],
    ["4", "1", "cmd", "0x81/0x98/...", "cmd", "cmd", "相同"],
    ["5", "1", "data_len 高字节", "0~255", "data_len>>8", "data_len>>8", "相同"],
    ["6", "1", "data_len 低字节", "0~255", "data_len", "data_len", "相同"],
    ["7..7+N-1", "N", "payload (data)", "变长", "Send_Buf[128..]", "payload", "相同"],
    ["7+N", "1", "CRC8", "sum&0xFF", "sum(payload)&0xFF", "sum(payload)&0xFF", "相同"],
]
write_sheet(
    "06_UART3_帧布局",
    ["偏移", "长度", "字段", "取值", "原 STM 写入", "Python 解析", "对比"],
    uart3_rows,
    col_widths=[12, 12, 22, 18, 28, 24, 12],
)


# ============================================================
# Sheet 8: UART3 图像帧 payload（cmd=0x81）
# ============================================================
img_payload_rows = [
    ["0~original-1", "original", "JPEG 压缩数据", "JPEG data",
     {"text": "send_image_to_PC: Send_Buf[128..128+orig-1]", "fmt": diff_old},
     {"text": "payload[0:jpeg_total]", "fmt": diff_new},
     "重新拼接顺序"],
    ["original~original+685", "686", "JPEG header (FF D8 ... FF D9)",
     "JPEG header",
     {"text": "Send_Buf[128+orig..128+orig+685]", "fmt": diff_old},
     {"text": "payload[jpeg_total:jpeg_total+686]", "fmt": diff_new},
     "重新拼接顺序"],
    ["original+686~original+813", "128", "DeviceInfo",
     "设备信息",
     {"text": "Send_Buf[128+orig+686..128+orig+813]", "fmt": diff_old},
     {"text": "payload[jpeg_total+686:]", "fmt": diff_new},
     "相同"],
]
write_sheet(
    "07_UART3_图像payload",
    ["偏移", "长度", "字段", "内容", "原 STM 写入位置", "Python 解析位置", "对比"],
    img_payload_rows,
    col_widths=[24, 12, 28, 14, 44, 36, 16],
)


# ============================================================
# Sheet 9: DeviceInfo 字段布局
# ============================================================
device_info_rows = [
    ["0~4", "5", "数据头标记", "00 55 AA 88 99",
     "data_head_info_copy", "✅ 读取", "相同"],
    ["5~6", "2", "VERSION_MAIN/SUB (位置1)", "0~255",
     "version_info_copy[5..6]", "备选读", "兼容"],
    ["9~10", "2", "VERSION_MAIN/SUB (位置2)", "0~255",
     "version_info_copy[9..10]", "优先读", "兼容"],
    ["11~18", "8", "胶囊 SN", "8 字节",
     "device_info_handle 读 Rec_Buf[orig+748..orig+811]", "✅ 读取", "相同"],
    ["27~38", "12", "MCU ID", "12 字节",
     "MCU_info_copy", "✅ 读取", "相同"],
    ["45~47", "3", "系统时间戳 (秒, 小端)", "0~16777215",
     "SD_Card_storage_imgdata", "✅ 读取", "相同"],
    ["61", "1", "VERSION_TEST (位置1)", "0~255",
     "未直接写", "备选读", "兼容"],
    ["63", "1", "VERSION_TEST (位置2)", "0~255",
     "version_info_copy[63]", "优先读", "兼容"],
    ["65~67", "3", "图像字节数 (小端)", "0~16777215",
     "SD_Card_storage_imgdata: Send_data_len", "✅ 读取", "相同"],
    ["70~76", "7", "RTC 时间 (BCD)", "20YY-MM-DD HH:MM:SS 周",
     "SD_Card_storage_imgdata", "✅ 读取", "相同"],
    ["83~89", "7", "RTC 上次更新时间 (持久化)", "BCD",
     "Read_Rtc_From_SD_Card", "— 未读取", "可加"],
]
write_sheet(
    "08_DeviceInfo_布局",
    ["偏移", "长度", "字段", "格式/范围", "原 STM 写入", "Python 解析", "对比"],
    device_info_rows,
    col_widths=[12, 12, 28, 24, 48, 22, 12],
)


# ============================================================
# Sheet 10: .YS 文件结构
# ============================================================
ys_rows = [
    ["文件头", "0~200639", "200640", "10 份帧大小",
     "SD_Card_write_file_header",
     "FRAME_HEADER_SIZE * 10", "同"],
    ["DS 标记", "0~9", "10", "DS + 8x00",
     "fileHeaderBuff[0..10]", "DS + 8x00", "同"],
    ["固定头", "10~15", "6", "FF 55 12 34 AB DC",
     "fileHeaderBuff[10..16]", "FF 55 12 34 AB DC", "同"],
    ["版本号", "16", "1", "0x02 (V2.0)",
     "fileHeaderBuff[16]", "0x02", "同"],
    ["FRAME_SIZE", "17~19", "3", "0x4E60 (20064, 小端)",
     "fileHeaderBuff[17..20]", "20064", "同"],
    ["FRAME_HEADER_SIZE", "20~21", "2", "0x80 (128, 小端)",
     "fileHeaderBuff[20..22]", "128", "同"],
    ["胶囊类型", "22", "1", "0x00 (肠) / 0x01 (胃)",
     "fileHeaderBuff[22]", "0x00/0x01", "同"],
    ["FILE_HEADER_SIZE", "23~25", "3", "0x31080 (200640, 小端)",
     "fileHeaderBuff[23..26]", "200640", "同"],
    ["每帧", "200640+0..200640+20063", "20064", "1 帧图像",
     "SD_Card_storage_imgdata", "YS_FRAME_SIZE", "同"],
    ["帧 DevInfo", "200640+0..+127", "128", "覆盖前 128B",
     "Send_Buf[i] = DeviceInfo[i]", "✅ 已修正读取位置", "修正"],
    ["帧 JPEG header", "200640+128..+813", "686", "FF D8 ... FF D9",
     "模板保留", "✅ 已修正拼接顺序", "修正"],
    ["帧 JPEG 数据", "200640+814..+20063", "19250", "JPEG 压缩数据",
     "Rec_Buf[814..]", "✅ 已修正读取位置", "修正"],
]
write_sheet(
    "09_YS_文件结构",
    ["区域", "偏移", "长度", "值/格式", "原 STM 操作", "Python 操作", "对比"],
    ys_rows,
    col_widths=[16, 22, 12, 24, 32, 32, 12],
)


# ============================================================
# Sheet 11: RF 物理层参数
# ============================================================
rf_rows = [
    ["频点", "2400 MHz", "RADIO->FREQUENCY = 0u", "相同"],
    ["速率", "2 Mbit", "RADIO->MODE = Nrf_2Mbit", "相同"],
    ["包长", "254 字节", "RADIO_PACKET_SIZE", "相同"],
    ["PREFIX0", "0xC4C3C2E7", "RADIO->PREFIX0", "相同"],
    ["PREFIX1", "0xC5C6C7C8", "RADIO->PREFIX1", "相同"],
    ["BASE0", "0xE7E7E7E7", "RADIO->BASE0", "相同"],
    ["BASE1", "0x00C2C2C2", "RADIO->BASE1", "相同"],
    ["TXADDRESS", "0", "RADIO->TXADDRESS", "相同"],
    ["RXADDRESSES", "1 (启用 BASE0)", "RADIO->RXADDRESSES", "相同"],
    ["BALEN", "4 (地址字节数)", "PCNF1_BALEN", "相同"],
    ["ENDIAN", "Big", "PCNF1_ENDIAN_Big", "相同"],
    ["WHITEEN", "Disabled (无白化)", "PCNF1_WHITEEN_Disabled", "相同"],
    ["CRC 长度", "2 字节 (16 位)", "CRCCNF_LEN_Two", "相同"],
    ["CRCINIT", "0xFFFF", "RADIO->CRCINIT", "相同"],
    ["CRCPOLY", "0x11021", "RADIO->CRCPOLY", "相同"],
    ["TX 功率 (TX 板)", "+4 dBm", "TXPOWER_Pos4dBm", "相同"],
    ["TX 功率 (RX 板 ACK)", "-8 dBm", "RECEIVER_ACK_TX_POWER", "相同"],
    ["RX 中断", "END event", "RADIO_INTENSET_END_Msk", "相同"],
    ["RX shorts", "READY_START + ADDRESS_RSSISTART",
     "SHORTS = READY_START_Msk | ADDRESS_RSSISTART_Msk", "相同"],
]
write_sheet(
    "10_RF_物理层",
    ["参数", "值", "寄存器/字段", "对比"],
    rf_rows,
    col_widths=[24, 28, 44, 12],
)


# ============================================================
# Sheet 12: 关键参数对照
# ============================================================
param_rows = [
    ["采集周期", "500 ms", "IMAGE_PERIOD_MS", "相同"],
    ["分片间隔", "1 ms", "IMAGE_FRAGMENT_GAP_MS", "相同"],
    ["重发间隔", "20 ms", "IMAGE_PASS_GAP_MS", "相同"],
    ["重发遍数", "0 (1 遍)", "IMAGE_BLOCK_PASSES = 1", "相同"],
    ["ACK 超时", "30 ms", "IMAGE_ACK_TIMEOUT_MS", "相同"],
    ["重试次数", "0 (原) / 1 (现)", "IMAGE_MAX_RETRIES", "差异（当前更抗丢包）"],
    ["采集超时", "1000 ms", "IMAGE_CAPTURE_TIMEOUT_MS", "相同"],
    ["看门狗", "3 s", "WATCHDOG_TIMEOUT_SECONDS", "相同"],
    ["UART3 波特率", "1 Mbps", "UART_BAUDRATE_BAUDRATE_Baud1M", "相同"],
    ["RX 队列深度", "8", "RADIO_QUEUE_DEPTH", "相同"],
    ["TX 队列深度", "4", "RADIO_QUEUE_DEPTH", "相同"],
    ["Radio IRQ 优先级", "6", "RADIO_IRQ_PRIORITY", "相同"],
    ["UART IRQ 优先级", "5", "UART_IRQ_PRIORITY", "相同"],
    ["RF1662 天线数", "12", "RF1662_ANTENNA_COUNT", "相同"],
    ["RF1662 扫描 dwell", "50~100 ms", "RF1662_SCAN_DWELL_MS", "相同"],
]
write_sheet(
    "11_关键参数",
    ["参数", "值", "宏/字段", "对比"],
    param_rows,
    col_widths=[24, 28, 44, 32],
)


# ============================================================
# Sheet 13: 代码位置对照
# ============================================================
code_rows = [
    ["Radio 包长度/载荷", "drv_radio.h::PACKET_SIZE", "legacy_protocol.h::LEGACY_RADIO_PACKET_SIZE", "同义"],
    ["命令字常量", "common/protocol.h", "legacy_protocol.h", "同义 (LEGACY_ 前缀)"],
    ["图像数据 buffer", "image_buff[FRAME_DATA_SIZE]", "image_buff[FRAME_DATA_SIZE] (TX)", "同义"],
    ["图像采集任务", "image.c::image_capture_task()", "nrf_tx/image.c::image_capture_task()", "同名"],
    ["分片发送函数", "image.c::image_data_pkt_send()",
     "nrf_tx/image.c::image_tx_service() (内联)", "重命名+合并"],
    ["BEGIN 包组装", "image.c::image_begin_pkt_send()",
     "nrf_tx/image.c::image_tx_service() (内联)", "重命名+合并"],
    ["END 包组装", "image.c::image_end_pkt_send()",
     "nrf_tx/image.c::image_tx_service() (内联)", "重命名+合并"],
    ["ACK 超时重发", "无 (原工程未实现)",
     "nrf_tx/image.c::image_ack_service()", "新增"],
    ["SN 全局变量", "capsule_sn_out (UINT8*)",
     "g_capsule_sn[8] + capsule_sn_storage_*", "重构为独立模块"],
    ["SN 存储接口", "内联在 capsule_sn_set_checking()",
     "capsule_sn_storage.h/c (独立)", "独立模块"],
    ["Flash 读写", "drv_flash.h/c (自实现)",
     "drv_flash.h/c (基于 nrf_nvmc 重写)", "底层依赖变更"],
    ["Radio 接收", "drv_radio.c::radio_receive_one_packet()",
     "nrf_rx/image.c::receiver_forward_one()", "重命名"],
    ["图像协议解析", "image.c::image_begin_pkt_parse()",
     "nrf_rx/image.c::receiver_process_legacy_packet()", "重命名"],
    ["ACK 发送", "usart_transfer_station.c::capsule_sn_CMDDeal() (经 UART2)",
     "nrf_rx/image.c::receiver_send_image_ack() (直接 Radio)", "路径变更"],
    ["STM UART3 协议", "drv_usart.c::USART3_Send()",
     "未改动（沿用原 STM 固件）", "无改动"],
    ["SD 卡存储", "storage_card.c::SD_Card_storage_imgdata()",
     "未改动（沿用原 STM 固件）", "无改动"],
    ["上位机显示", "无 (计划中)",
     "tools/tygd31_viewer/ (Python + PyQt5)", "新增"],
    ["设备信息组装", "device_info.c::device_info_rewrite()",
     "未改动（STM 固件不变）", "无改动"],
]
write_sheet(
    "12_代码位置",
    ["功能", "原 TYGD31 位置", "当前实现位置", "备注"],
    code_rows,
    col_widths=[20, 40, 50, 24],
)


# ============================================================
# Sheet 14: 修订记录
# ============================================================
changelog = [
    ["v1.0", "2026-09-08", "初版",
     "整合原 TYGD31 协议与当前 nrf_tx/nrf_rx/STM 实现对照，13 个 Sheet"],
    ["", "", "", ""],
    ["", "已修复差异", "", ""],
    ["1", "2026-09-08", "BEGIN 包 byte 14-16 版本号", "原工程有 → 修复前漏写 → 已补"],
    ["2", "2026-09-08", "DeviceInfo 版本号/VERSION_TEST 偏移兼容", "原 5-6/61 → 现兼容 9-10/63"],
    ["3", "2026-09-08", ".YS 帧 JPEG header/data 顺序", "修正读取逻辑（header 在前）"],
    ["4", "2026-09-08", "UART3 payload JPEG 顺序", "修正重组（header 在前）"],
    ["", "", "", ""],
    ["", "已知问题", "", ""],
    ["P1", "TX ACK 链路", "丢包率 90%", "详见 TIMING_ANALYSIS.md 章节 13"],
    ["P2", "RX ACK TXEN wait", "150 µs vs 理论 1 ms", "需排查 Radio 状态机"],
    ["", "", "", ""],
    ["", "未实现", "", ""],
    ["TODO", "capsule_sn_storage_set_checking",
     "TX 远程接收绑定命令", "接口已预留"],
]
write_sheet(
    "13_修订记录",
    ["编号", "日期", "项目", "说明"],
    changelog,
    col_widths=[10, 16, 36, 60],
)


wb.close()
print(f"Generated: {OUT_PATH}")
print(f"Size: {os.path.getsize(OUT_PATH)} bytes")
