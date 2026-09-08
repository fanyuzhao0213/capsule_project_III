"""Quick test of YS frame parser."""
import sys
sys.path.insert(0, "e:/Project/capsule_project/tools/tygd31_viewer")

import struct

# 文件头
ys_file_header = bytearray(200640)
ys_file_header[0:10] = b"DS" + b"\x00" * 8
ys_file_header[10:16] = b"\xff\x55\x12\x34\xab\xdc"
ys_file_header[16] = 0x02

# 最小 JPEG header（686 字节）+ 最小 JPEG body
jpeg_header = b"\xff\xd8" + b"\x00" * 684  # 686 bytes

jpeg_body = (
    b"\xff\xe0\x00\x10JFIF\x00\x01\x01\x00\x00\x01\x00\x01\x00\x00"
    b"\xff\xdb\x00C\x00" + bytes(64) +
    b"\xff\xc0\x00\x0b\x08\x00\x10\x00\x10\x01\x01\x11\x00"
    b"\xff\xc4\x00\x14\x00\x01\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"
    b"\xff\xda\x00\x08\x01\x01\x00\x00\x3f\x00"
    + b"\x7f" * 256 +
    b"\xff\xd9"
)
print(f"jpeg_body length: {len(jpeg_body)}")

target_data_len = 20064 - 128 - 686  # 19250
jpeg_data = jpeg_body + b"\x00" * (target_data_len - len(jpeg_body))
jpeg_data = jpeg_data[:target_data_len]
print(f"jpeg_data length: {len(jpeg_data)}")

# DeviceInfo
device_info = bytearray(128)
device_info[0:5] = b"\x00\x55\xaa\x88\x99"
device_info[5] = 1
device_info[6] = 1
device_info[11:19] = bytes.fromhex("1122334455667788")
device_info[45:48] = struct.pack("<I", 12345)[:3]
device_info[65:68] = struct.pack("<I", len(jpeg_body) + 2)[:3]
device_info[70] = 26
device_info[71] = 9
device_info[72] = 8
device_info[73] = 2
device_info[74] = 14
device_info[75] = 30
device_info[76] = 45

frame = bytes(device_info) + jpeg_header + jpeg_data
print(f"frame length: {len(frame)} (expected 20064)")
assert len(frame) == 20064

with open("e:/Project/capsule_project/tools/tygd31_viewer/test_sample.YS", "wb") as f:
    f.write(bytes(ys_file_header))
    f.write(frame)

# 测试解析
from frame_parser import parse_ys_file, device_info_to_text

frames = parse_ys_file("e:/Project/capsule_project/tools/tygd31_viewer/test_sample.YS")
print(f"\nParsed {len(frames)} frames")
for i, fr in enumerate(frames):
    print(f"\nFrame {i}: jpeg={fr.jpeg_size}B")
    print(device_info_to_text(fr.device_info))

# 测试 JPEG 解码
print("\n--- JPEG decode test ---")
print(f"JPEG header (first 16 bytes): {frames[0].jpeg[:16].hex()}")
print(f"JPEG body starts with: {frames[0].jpeg[686:706].hex()}")

try:
    from PyQt5.QtGui import QPixmap
    pix = QPixmap()
    ok = pix.loadFromData(frames[0].jpeg, "JPEG")
    print(f"QPixmap JPEG decode: {'OK' if ok else 'FAIL'} ({pix.width()}x{pix.height()})")
except Exception as e:
    print(f"Decode error: {e}")
