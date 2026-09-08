"""
frame_parser.py
STM32 → PC UART3 帧解析 + .YS 文件解析（共用）

UART3 帧格式（STM → PC）：
  +-----+-----+-----+-----+-----+-------+-------+
  | FF  | 55  | 12  | 34  | cmd | lenHI | lenLO |  (7B header)
  +-----+-----+-----+-----+-----+-------+-------+
  | data[0..N-1]                                  |  (N bytes)
  +-----------------------------------------------+
  | CRC8                                          |  (1 byte)
  +-----------------------------------------------+

UART3 payload（cmd=0x81 IMG_FORWARD）：
  +----------+--------------+--------------+
  | JPEG数据 | JPEG header   | DeviceInfo   |
  | (变长)   | (686 bytes)  | (128 bytes)  |
  |          | FF D8 ... FF |              |
  +----------+--------------+--------------+

  注：JFIF JPEG 文件由 header + 数据 组成，原始流把数据放在前面，
      header 放在后面以便按帧重组。重组时需要调换顺序：
      full_jpeg = jpeg_header + jpeg_data

.YS 文件帧结构（20064 bytes）：
  +-----------+----------------+----------+
  | DevInfo   | JPEG header    | JPEG数据 |
  | (128B)    | (686 bytes)    | (变长)   |
  +-----------+----------------+----------+
"""

from __future__ import annotations
import os
from dataclasses import dataclass, field
from typing import Optional, List, Tuple


# 协议常量（与 STM 端 drv_usart.c 对齐）
FRAME_HEADER_BYTES = bytes([0xFF, 0x55, 0x12, 0x34])

CMD_IMG_FORWARD = 0x81
CMD_TEXT_INFO = 0x98   # TFC: / TFFC: 等 ASCII 文本

JPEG_HEADER_LEN = 686
FRAME_HEADER_INFO_LEN = 128   # DeviceInfo 长度

# YS 文件
YS_FRAME_SIZE = 20064         # 每帧字节数
YS_FILE_HEADER_SIZE = 200640  # 文件起始头（10 × 20064）


@dataclass
class ImageFrame:
    """一帧完整的图像数据。"""
    jpeg: bytes = b""                 # 还原后的完整 JPEG 文件
    device_info: bytes = b""          # 128 字节 DeviceInfo
    cmd: int = 0                      # 命令字
    raw_len: int = 0                  # 原始 payload 长度
    timestamp: float = 0.0            # 接收时间戳

    @property
    def jpeg_size(self) -> int:
        return len(self.jpeg)


@dataclass
class ParseState:
    """流式解析状态机（支持跨包解析 UART3 帧）。"""
    buf: bytearray = field(default_factory=bytearray)

    def feed(self, data: bytes) -> List[ImageFrame]:
        """喂入一段字节流，返回解析出的所有完整帧。"""
        self.buf.extend(data)
        frames: List[ImageFrame] = []
        while True:
            frame, consumed = try_parse_one(bytes(self.buf))
            if frame is None:
                break
            frames.append(frame)
            del self.buf[:consumed]
        # 防止 buf 无限增长（异常帧超过 64 KB 强制丢弃）
        if len(self.buf) > 65536:
            del self.buf[:]
        return frames


def try_parse_one(buf: bytes) -> Tuple[Optional[ImageFrame], int]:
    """
    尝试从 buf 头部解析一帧 UART3 数据。
    返回 (frame, consumed)；frame=None 时 consumed=0 表示需要更多数据。
    """
    if len(buf) < 7:
        return None, 0

    # 检查帧头 0xFF 0x55 0x12 0x34
    if buf[0:4] != FRAME_HEADER_BYTES:
        idx = find_header(buf, 1)
        if idx < 0:
            return None, len(buf)
        return None, idx

    cmd = buf[4]
    data_len = (buf[5] << 8) | buf[6]
    total_len = 7 + data_len + 1  # 头 + 数据 + CRC

    if len(buf) < total_len:
        return None, 0

    payload = bytes(buf[7:7 + data_len])
    crc_rx = buf[7 + data_len]
    crc_calc = sum(payload) & 0xFF
    if crc_rx != crc_calc:
        idx = find_header(buf, 7)
        if idx < 0:
            return None, len(buf)
        return None, idx

    frame = ImageFrame(
        cmd=cmd,
        raw_len=data_len,
        device_info=b"",
        timestamp=0.0,
    )

    if cmd == CMD_IMG_FORWARD:
        if data_len < JPEG_HEADER_LEN + FRAME_HEADER_INFO_LEN:
            return frame, total_len

        # payload 布局：[JPEG数据][JPEG header 686B][DeviceInfo 128B]
        jpeg_total = data_len - JPEG_HEADER_LEN - FRAME_HEADER_INFO_LEN
        jpeg_data = payload[0:jpeg_total]
        jpeg_header = payload[jpeg_total:jpeg_total + JPEG_HEADER_LEN]
        device_info = payload[jpeg_total + JPEG_HEADER_LEN:
                              jpeg_total + JPEG_HEADER_LEN + FRAME_HEADER_INFO_LEN]
        # 还原成完整 JPEG 文件（必须 header 在前）
        frame.jpeg = jpeg_header + jpeg_data
        frame.device_info = device_info
    elif cmd == CMD_TEXT_INFO:
        frame.jpeg = payload  # ASCII 文本
        frame.device_info = b""
    else:
        frame.jpeg = payload  # 未知命令，原样保留

    return frame, total_len


def find_header(buf: bytes, start: int) -> int:
    """在 buf 中从 start 开始查找 FRAME_HEADER_BYTES。"""
    needle = FRAME_HEADER_BYTES
    i = start
    while i + 4 <= len(buf):
        if buf[i:i + 4] == needle:
            return i
        i += 1
    return -1


def parse_ys_file(path: str) -> List[ImageFrame]:
    """
    解析 SD 卡上的 .YS 文件，按帧切分。
    文件结构：200640 字节文件头 + 多个 20064 字节帧。
    每帧结构：[DeviceInfo 128B][JPEG header 686B][JPEG 数据 变长]
    """
    frames: List[ImageFrame] = []
    try:
        with open(path, "rb") as f:
            data = f.read()
    except OSError as e:
        print(f"[parse_ys_file] cannot open {path}: {e}")
        return frames

    if len(data) <= YS_FILE_HEADER_SIZE:
        return frames

    pos = YS_FILE_HEADER_SIZE
    while pos + YS_FRAME_SIZE <= len(data):
        chunk = data[pos:pos + YS_FRAME_SIZE]
        frame = parse_ys_frame(chunk)
        if frame is not None:
            frames.append(frame)
        pos += YS_FRAME_SIZE
    # 处理文件末尾不足一帧的尾部（如有）
    if pos < len(data):
        tail = data[pos:]
        print(f"[parse_ys_file] 尾部不完整 {len(tail)} bytes 已忽略")
    return frames


def parse_ys_frame(chunk: bytes) -> Optional[ImageFrame]:
    """
    解析单个 20064 字节的 YS 帧。
    布局：[DeviceInfo 128B][JPEG header 686B][JPEG 数据 变长]
    """
    if len(chunk) != YS_FRAME_SIZE:
        return None

    device_info = chunk[0:FRAME_HEADER_INFO_LEN]
    jpeg_header = chunk[FRAME_HEADER_INFO_LEN:
                        FRAME_HEADER_INFO_LEN + JPEG_HEADER_LEN]
    jpeg_data = chunk[FRAME_HEADER_INFO_LEN + JPEG_HEADER_LEN:]

    # 还原成完整 JPEG 文件（header 在前）
    full_jpeg = jpeg_header + jpeg_data
    return ImageFrame(
        jpeg=full_jpeg,
        device_info=device_info,
        cmd=CMD_IMG_FORWARD,
        raw_len=YS_FRAME_SIZE,
    )


def device_info_to_text(info: bytes) -> str:
    """把 128 字节 DeviceInfo 解析成可读字符串。"""
    if len(info) < FRAME_HEADER_INFO_LEN:
        return f"<short DeviceInfo: {len(info)} bytes>"
    lines = []
    # bytes 0-4: data_head_info（0x00, 0x55, 0xAA, 0x88, 0x99）
    lines.append(f"数据头: {info[0:5].hex(' ').upper()}")
    # bytes 5-6: VERSION_MAIN/SUB（实际 STM 把版本号放在 9-10，这里兼容两套）
    main_v = info[5]
    sub_v = info[6]
    if main_v == 0 and sub_v == 0:
        # 改用 9-10 偏移
        main_v = info[9]
        sub_v = info[10]
    lines.append(f"固件版本: {main_v}.{sub_v}")
    # bytes 11-18: 胶囊 SN
    sn = info[11:19]
    lines.append(f"胶囊SN: {sn.hex(' ').upper()}")
    # bytes 27-38: MCU ID
    if len(info) >= 39:
        mcu_id = info[27:39]
        lines.append(f"MCU ID: {mcu_id.hex(' ').upper()}")
    # bytes 45-47: 系统时间戳（秒数）
    if len(info) >= 48:
        ts = info[45] | (info[46] << 8) | (info[47] << 16)
        lines.append(f"系统时间戳: {ts} s")
    # bytes 61: VERSION_TEST（也可能放在 63）
    test_v = info[61]
    if test_v == 0:
        test_v = info[63]
    lines.append(f"测试版本: {test_v}")
    # bytes 65-67: 图像长度
    if len(info) >= 68:
        img_len = info[65] | (info[66] << 8) | (info[67] << 16)
        lines.append(f"JPEG长度: {img_len} bytes")
    # bytes 70-76: RTC 时间（BCD 格式：年从 2000 起）
    if len(info) >= 77 and any(info[70:77]):
        lines.append(
            f"RTC时间: 20{info[70]:02X}-{info[71]:02X}-{info[72]:02X} "
            f"{info[73]&0x7F:02X}:{info[74]:02X}:{info[75]:02X} (星期{info[76]})"
        )
    return "\n".join(lines)


def list_ys_files(root: str) -> List[str]:
    """列出 root 目录下的所有 .YS 文件（不递归）。"""
    try:
        entries = os.listdir(root)
    except OSError:
        return []
    return sorted(
        os.path.join(root, e)
        for e in entries
        if e.upper().endswith(".YS") and os.path.isfile(os.path.join(root, e))
    )

