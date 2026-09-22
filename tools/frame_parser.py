"""
frame_parser.py
STM32 → PC UART3 帧解析 + .YS 文件解析（共用）

UART3 帧格式（STM → PC）：
  +-----+-----+-----+-----+-----+-------+-------+
  | 5A  | 41  | 59  | 53  | cmd | lenHI | lenLO |  (7B header)
  +-----+-----+-----+-----+-----+-------+-------+
  | data[0..N-1]                                  |  (N bytes)
  +-----------------------------------------------+
  | CRC8                                          |  (1 byte)
  +-----------------------------------------------+

UART3 payload（cmd=0x81 IMG_FORWARD）：
  +--------------------------+--------------+
  | 完整JPEG（FF D8...FF D9） | DeviceInfo   |
  | (变长)                   | (128 bytes)  |
  +--------------------------+--------------+

.YS 文件帧结构（20064 bytes）：
  +-----------+----------------+----------+
  | DevInfo   | JPEG header    | JPEG数据 |
  | (128B)    | (686 bytes)    | (变长)   |
  +-----------+----------------+----------+
"""

from __future__ import annotations
import os
import time
from dataclasses import dataclass, field
from typing import Optional, List, Tuple


# 协议常量（与 STM 端 drv_usart.c 对齐）
FRAME_HEADER_BYTES = bytes([0x5A, 0x41, 0x59, 0x53])

CMD_IMG_FORWARD = 0x81
CMD_TEXT_INFO = 0x98   # TFC: / TFFC: 等 ASCII 文本
CMD_SN_BROADCAST = 0x05

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
                if consumed > 0:
                    del self.buf[:consumed]
                    continue
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

    # 检查当前STM32输出的ZAYS帧头：0x5A 0x41 0x59 0x53
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
        if data_len <= FRAME_HEADER_INFO_LEN:
            return frame, total_len

        # 当前STM32发送布局：[完整JPEG][DeviceInfo 128B]。
        # JPEG已经是FF D8开头、FF D9结尾，不再拆分或重排686字节头部。
        jpeg_total = data_len - FRAME_HEADER_INFO_LEN
        frame.jpeg = payload[:jpeg_total]
        frame.device_info = payload[jpeg_total:]
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


def _u24_le(data: bytes) -> int:
    return data[0] | (data[1] << 8) | (data[2] << 16)


def _i16_be(data: bytes) -> int:
    value = (data[0] << 8) | data[1]
    return value - 0x10000 if value & 0x8000 else value


def _rtc_text(data: bytes) -> str:
    """RTC顺序：年、月、日、星期、时、分、秒；内容按BCD字节显示。"""
    if len(data) < 7:
        return "<无效>"
    return (
        f"20{data[0]:02X}-{data[1]:02X}-{data[2]:02X} "
        f"{data[4]:02X}:{data[5]:02X}:{data[6]:02X} 星期{data[3]:02X}"
    )


def device_info_to_text(
    info: bytes,
    previous: bytes | None = None,
    frame_number: int | None = None,
    received_at: float | None = None,
) -> str:
    """完整解析128字节DeviceInfo，并可列出相对上一包的变化。"""
    if len(info) < FRAME_HEADER_INFO_LEN:
        return f"<short DeviceInfo: {len(info)} bytes>"
    marker_ok = info[0:5] == bytes.fromhex("00 55 AA 88 99")
    lines: List[str] = []
    if frame_number is not None:
        stamp = time.strftime("%Y-%m-%d %H:%M:%S", time.localtime(received_at or time.time()))
        lines.append(f"第{frame_number}帧  {stamp}")
    lines.extend([
        f"标识 [00-04]: {info[0:5].hex(' ').upper()} ({'正常' if marker_ok else '异常'})",
        f"TX版本 [05-06,61]: {info[5]}.{info[6]}.{info[61]}",
        f"RX版本 [07-08,62]: {info[7]}.{info[8]}.{info[62]}",
        f"STM版本 [09-10,63]: {info[9]}.{info[10]}.{info[63]}",
        f"胶囊SN [11-18]: {info[11:19].hex(' ').upper()}",
        f"RX芯片ID [19-26]: {info[19:27].hex(' ').upper()}",
        f"STM芯片ID [27-38]: {info[27:39].hex(' ').upper()}",
        f"STM运行时间 [45-47]: {_u24_le(info[45:48])} s",
        f"当前天线 [49]: {info[49]}",
        f"无线频点 [50]: {2400 + info[50]} MHz (偏移 {info[50]})",
    ])
    rssi = list(info[39:45]) + list(info[51:57])
    rssi_text = "  ".join(
        f"{idx + 1}:{'-' + str(value) + ' dBm' if value else '无样本'}"
        for idx, value in enumerate(rssi)
    )
    lines.append(f"天线RSSI [39-44,51-56]: {rssi_text}")
    lines.extend([
        f"JPEG长度 [65-67]: {_u24_le(info[65:68])} B",
        f"RTC [70-76]: {_rtc_text(info[70:77])}",
        f"上次RTC [83-89]: {info[83:90].hex(' ').upper()}",
    ])
    if info[90] == 1:
        lines.append(
            "加速度 [90-96]: 有效  "
            f"X={_i16_be(info[91:93])}  "
            f"Y={_i16_be(info[93:95])}  "
            f"Z={_i16_be(info[95:97])} (原始LSB)"
        )
    else:
        lines.append("加速度 [90-96]: 无效")

    if previous is not None and len(previous) >= FRAME_HEADER_INFO_LEN:
        changed = [i for i in range(FRAME_HEADER_INFO_LEN) if info[i] != previous[i]]
        lines.append(f"与上一帧相比: {len(changed)} 字节变化")
        lines.append("变化偏移: " + (", ".join(f"{i:03d}" for i in changed) if changed else "无"))
        if changed:
            lines.append(
                "变化数值: " + "  ".join(
                    f"{i:03d} {previous[i]:02X}→{info[i]:02X}" for i in changed
                )
            )

    lines.append("")
    lines.append("128字节原始数据 (每行16字节):")
    for offset in range(0, FRAME_HEADER_INFO_LEN, 16):
        lines.append(f"{offset:03d}: {info[offset:offset + 16].hex(' ').upper()}")
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
