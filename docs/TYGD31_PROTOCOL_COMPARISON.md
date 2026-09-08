# TYGD31 协议对照文档

> 本文档对照「原始 TYGD31-2.4GHZ 工程」与「当前重构实现」在协议层面、数据链路、文件格式上的异同。

---

## 0. 系统角色与链路

```
┌──────────┐   2.4 GHz Radio    ┌──────────┐   UART 1 Mbps   ┌──────────┐   UART 1 Mbps   ┌─────┐
│ 胶囊     │ =================> │ RF 接收板 │ ==============> │ STM32     │ ==============> │ PC  │
│ (nRF52)  │ <================= │ (nRF52)   │ <============== │ 主控      │ <============== │     │
│ TX       │   254B / 包        │ RX        │   0xFF 55 12 34 帧          │              │
└──────────┘                   └──────────┘                   └────┬─────┘                   └─────┘
                                                                  │ FATFS / SD
                                                                  ▼
                                                          ┌──────────────┐
                                                          │ SD 卡 / U 盘  │
                                                          │ Y<SN>.YS 文件 │
                                                          └──────────────┘
```

| 角色 | 原 TYGD31 | 当前实现 |
|---|---|---|
| 胶囊发送板 | `TYGD31-2.4GHZ/TYGD_TX/`（Keil 工程） | `nrf_tx/examples/peripheral/radio/TX/`（重构） |
| RF 接收板 | `TYGD31-2.4GHZ/TYGD_RX/`（Keil 工程） | `nrf_rx/examples/peripheral/radio/receiver/`（重构） |
| STM32 主控 | `TYGD31-2.4GHZ/TYGD_STM/`（Keil 工程） | **未改动**（沿用原 STM 固件）|
| 上位机显示 | 计划中的 Qt 程序（未在原工程找到） | `tools/tygd31_viewer/`（Python + PyQt5） |

---

## 1. RF 链路协议（2.4 GHz Radio 包）

### 1.1 Radio 包物理层（两端共用）

| 参数 | 原 TYGD31 | 当前实现 |
|---|---|---|
| 包长度 | 254 字节（固定） | 254 字节（`RADIO_PACKET_SIZE`） |
| 频率 | `RADIO->FREQUENCY = 0u`（即 2400 MHz） | 同 |
| 速率 | `RADIO_MODE_MODE_Nrf_2Mbit` | 同 |
| 地址 | PREFIX0/1 + BASE0/1 + TXADDRESS | 同（值一致：`PREFIX0=0xC4C3C2E7` 等） |
| CRC | 16 位，初始值 0xFFFF，多项式 0x11021 | 同 |
| 白化 | Disabled | 同 |
| 字节序 | Big-endian（`PCNF1_ENDIAN_Big`） | 同 |

### 1.2 公共包头（10 字节）

```
偏移  长度  字段       说明
─────────────────────────────────────────
0     1     cmd        命令字
1     1     frame_id   帧 ID（0~255 循环）
2     8     capsule_sn 胶囊序列号（FICR 或 Flash 设定）
─────────────────────────────────────────
```

| 字段 | 原 TYGD31 TX `image.c` | 当前 nrf_tx `image.c` |
|---|---|---|
| cmd 填充 | `buff[pkt_len++] = CMD_xxx` | `m_tx_packet[0] = LEGACY_CMD_xxx` |
| frame_id | `buff[pkt_len++] = image_id` | `m_tx_packet[1] = frame_id` |
| capsule_sn | `capsule_sn_out[i]`（指向 Flash 或 DEVICE_ID） | `g_capsule_sn[i]`（指向 storage 的 active SN） |

### 1.3 命令字（CMD）

| 命令 | 值 | 方向 | 原 TYGD31 定义 | 当前实现 |
|---|---|---|---|---|
| 图像开始包 | `0x01` | TX → RX | `CMD_IMG_BEGIN_PACKET` | `LEGACY_CMD_IMAGE_BEGIN` |
| 图像结束包 | `0x03` | TX → RX | `CMD_IMG_END_PACKET` | `LEGACY_CMD_IMAGE_END` |
| 胶囊 SN 广播 | `0x05` | TX → RX | `CMD_CAPSULE_SN_BROADCAST` | `LEGACY_CMD_CAPSULE_SN_BROADCAST` |
| 胶囊 ID 广播 | `0x07` | TX → RX | `CMD_CAPSULE_ID_BROADCAST` | `LEGACY_CMD_CAPSULE_ID_BROADCAST` |
| 图像接收应答 | `0x10` | RX → TX | `CMD_IMG_RCVD_RSP` | `LEGACY_CMD_IMAGE_RECEIVED_RESPONSE` |
| 图像数据包 | `0x80` | TX → RX | `CMD_IMG_DATA_PACKET` | `LEGACY_CMD_IMAGE_DATA` |
| 图像转发命令 | `0x81` | RX → STM | `CMD_IMG_FORWARD` | `LEGACY_CMD_IMAGE_FORWARD` |

> 注：胶囊 SN 绑定相关命令（`0x20~0x2B`）在原 STM/TY 工程里走 UART2/UART3 中转，与 Radio 协议无关。当前实现 **尚未启用** 绑定命令监听（`capsule_sn_storage_set_checking()` 是 TODO）。

### 1.4 三种核心包的差异化字段

#### BEGIN（`0x01`）

| 偏移 | 长度 | 字段 | 原 TYGD31 写入 | 当前实现 |
|---|---|---|---|---|
| 10 | 2 | 图像总字节数（大端） | `img_data_len` | `block_size` |
| 12 | 2 | 包总数（大端） | `img_data_pkt_num` | `fragment_count` |
| 14 | 1 | **VERSION_MAIN** | `VERSION_MAIN` | `VERSION_MAIN`（新增） |
| 15 | 1 | **VERSION_SUB** | `VERSION_SUB` | `VERSION_SUB`（新增） |
| 16 | 1 | **VERSION_TEST** | `VERSION_TEST` | `VERSION_TEST`（新增） |

✅ **修复记录**：当前实现最初漏写了 byte 14-16 的版本号，已修复（`m_tx_packet[14..16] = VERSION_*`）。RX 端 `legacy_image_rx_t` 也新增了 `version_main/sub/test` 字段读取。

#### DATA（`0x80`）

| 偏移 | 长度 | 字段 | 原 TYGD31 写入 | 当前实现 |
|---|---|---|---|---|
| 10 | 2 | 分片编号（大端） | `i/256`、`i%256` | `LegacyProtocol_PutU16Be(fragment_index)` |
| 12 | 242 | JPEG 载荷 | `memcpy(buff+10, data, len)` | `cx93510_frame_buffer_read(...)` |

最后一包不足 242 字节时，剩余字节填 0；接收端按 `image_length - packet_index * 242` 截取实际有效数据。

#### END（`0x03`）

| 偏移 | 长度 | 字段 | 原 TYGD31 写入 | 当前实现 |
|---|---|---|---|---|
| 10 | 1 | 8 位累加校验和 | `image_data_check_sum` | `legacy_checksum` |

### 1.5 ACK 协议（`0x10`）

| 字段 | 大小 | 内容 |
|---|---|---|
| cmd | 1 | `0x10` |
| frame_id | 1 | 与 END 包 frame_id 一致 |
| capsule_sn | 8 | 来自该帧 BEGIN 包 |

```
原 TYGD31 RX: capsule_sn_CMDDeal() 通过 UART2 中转 → STM
当前实现 RX: receiver_send_image_ack() 直接 Radio 回 TX
```

⚠️ **已知问题**：当前实现 ACK 链路丢包率较高（实测 ~90%），TX 端日志显示 `TXEN wait=3152` 偏小（约 150 µs vs 理论 1 ms），具体根因排查见 [TIMING_ANALYSIS.md](../nrf_tx/examples/peripheral/radio/TX/TIMING_ANALYSIS.md) 章节 13。

---

## 2. UART3 协议（STM 主控 → PC）

### 2.1 帧格式

```
+--------+--------+--------+--------+--------+--------+--------+
| 0xFF   | 0x55   | 0x12   | 0x34   |  cmd   | lenHI  | lenLO  |   ← 7 字节头
+--------+--------+--------+--------+--------+--------+--------+
| data[0..N-1]                                            |   ← N 字节 payload
+---------------------------------------------------------+
| CRC8                                                    |   ← 1 字节校验和
+---------------------------------------------------------+
```

- **CRC8**：`sum(payload) & 0xFF`（普通累加）
- 端口：**USART3**（TX=P0.15, RX=P0.16），1 Mbps 8N1
- 实现位置：原 STM `drv_usart.c::USART3_Send()`，当前 STM 固件不变

### 2.2 图像帧 payload（`cmd = 0x81`）

```
+-----------+----------------+-------------+
| JPEG 数据 | JPEG header    | DeviceInfo  |
| (变长)    | (686 bytes)    | (128 bytes) |
|           | FF D8 ... FF   |             |
+-----------+----------------+-------------+
```

- **JPEG 数据长度** = `lenHI/lenLO` 字段值 − 686 − 128
- **JPEG header**（686 字节）：JFIF 标准头（FF D8 + APP0 + DQT + DHT + SOF + SOS + …）
- **DeviceInfo**（128 字节）：MCU 信息 + 固件版本 + SN + RTC 时间 + 图像长度等

### 2.3 DeviceInfo 字段定义（128 字节）

| 偏移 | 长度 | 字段 | 原 STM 写入位置 | 当前 Python 读取位置 |
|---|---|---|---|---|
| 0~4 | 5 | 数据头标记 `00 55 AA 88 99` | `device_info.c::data_head_info_copy` | ✅ |
| 5~6 | 2 | VERSION_MAIN/SUB | `device_info.c::version_info_copy` | ⚠️ 实际也在 9~10 |
| 9~10 | 2 | VERSION_MAIN/SUB（备用） | `device_info.c::version_info_copy` | ✅（优先读 5-6，若 0 则读 9-10）|
| 11~18 | 8 | 胶囊 SN | `device_info_handle` | ✅ |
| 27~38 | 12 | MCU ID | `device_info.c::MCU_info_copy` | ✅ |
| 45~47 | 3 | 系统时间戳（秒） | `SD_Card_storage_imgdata` | ✅ |
| 61 | 1 | VERSION_TEST（备用） | 未直接写 | ⚠️ 实际在 63 |
| 63 | 1 | VERSION_TEST | `device_info.c::version_info_copy` | ✅ |
| 65~67 | 3 | 图像字节数（小端） | `SD_Card_storage_imgdata` | ✅ |
| 70~76 | 7 | RTC 时间（BCD） | `SD_Card_storage_imgdata` | ✅ |
| 83~89 | 7 | RTC 上次更新时间（持久化） | `Read_Rtc_From_SD_Card` | — |

> 兼容性：当前 Python 解析器对版本号（5-6 vs 9-10）、VERSION_TEST（61 vs 63）做了**双位置兼容读取**，避免老固件数据无法解析。

### 2.4 文本帧 payload（`cmd = 0x98`）

- 直接透传 ASCII 文本（如 `TFC:12345 KB` / `TFFC:6789 B`）
- 由 `Get_Card_Free_Capacity()` 发送

---

## 3. .YS 文件格式（SD 卡 / U 盘）

### 3.1 文件结构

```
┌──────────────────────────────────────────────────────┐
│ 文件头 200640 字节（FILE_HEADER_SIZE = 20064 × 10）   │  ← SD_Card_write_file_header()
│   - "DS\0\0\0\0\0\0\0\0" 标记                        │
│   - 0xFF 0x55 0x12 0x34 0xAB 0xDC + 版本号           │
├──────────────────────────────────────────────────────┤
│ 帧 1（20064 字节）                                   │  ← SD_Card_storage_imgdata()
│ 帧 2（20064 字节）                                   │
│ ...                                                  │
├──────────────────────────────────────────────────────┤
│ 文件结束                                              │
└──────────────────────────────────────────────────────┘
```

文件名格式：`0:Y<7字节SN>.YS`（例：`0:Y1122334455667788.YS`）

### 3.2 文件头（200640 字节）

| 偏移 | 字段 | 值 |
|---|---|---|
| 0~9 | 字符串标记 | `D` `S` `\0` × 8 |
| 10~13 | 文件头标志 | `0xFF 0x55 0x12 0x34` |
| 14~15 | 扩展标志 | `0xAB 0xDC` |
| 16 | 文件格式版本 | `0x02`（V2.0） |
| 17~19 | FRAME_SIZE（小端 3B） | `0x4E60`（20064） |
| 20~21 | FRAME_HEADER_SIZE（小端 2B） | `0x80`（128） |
| 22 | 胶囊类型 | `0x00`（肠）/`0x01`（胃） |
| 23~25 | FILE_HEADER_SIZE（小端 3B） | `0x31080`（200640） |

### 3.3 帧结构（每帧 20064 字节）

```
┌──────────┬────────────────┬──────────────┐
│ DevInfo  │ JPEG header    │ JPEG 数据    │
│ 128 字节 │ 686 字节       │ 19250 字节   │
│ (写时覆盖)│ FF D8 ... FF   │ (变长)       │
└──────────┴────────────────┴──────────────┘
```

- DevInfo：`Send_Buf[i] = DeviceInfo[i]` 覆盖前 128 字节（与模板前 128 字节不同位置）
- JPEG header：保留模板初始化时的 JFIF 标准头
- JPEG 数据：从 `Data_Buf0[814]` 开始存放接收到的压缩数据

### 3.4 关键宏定义

| 原 STM 定义 | 当前 Python 常量 | 值 |
|---|---|---|
| `FRAME_HEADER_SIZE` | `FRAME_HEADER_INFO_LEN` | 128 |
| `FRAME_DATA_SIZE` | （不再使用，由 layout 计算） | 20000 |
| `FRAME_SIZE` | `YS_FRAME_SIZE` | 20064 |
| `FILE_HEADER_SIZE` | `YS_FILE_HEADER_SIZE` | 200640 |
| `IMAGE_HEADER_LEN` | `JPEG_HEADER_LEN` | 686 |
| `BUFF_SIZE` | — | 20264（接收缓冲） |

---

## 4. 数据链路时序

### 4.1 单帧完整生命周期

```
┌──────┐   RF    ┌──────┐   UART3   ┌─────┐    U盘   ┌─────┐
│ 胶囊 │         │  RF  │           │ STM │          │  PC │
│ TX   │ ========>│ RX   │ ==========>│     │ ========>│    │
└──────┘         └──────┘           └─────┘          └─────┘
                                   同时: SD卡
                                   写 .YS 文件
```

| 阶段 | 原 TYGD31 | 当前实现 |
|---|---|---|
| ① 采集图像 | CX93510 SPI + OV7676 | 同（驱动未改）|
| ② Radio 分片发送 | 254B/包 × N 包 + BEGIN×2 + END | 同 |
| ③ RX 重组 | 收 N+1 包 → 完整 JPEG | 同 |
| ④ ACK | RX 发 0x10 给 TX | 同（**当前有丢包问题**）|
| ⑤ RX → STM | UART3 透传 254B/包 | 同（STM 固件不变）|
| ⑥ STM 存 SD | FATFS 追加 20064B/帧 | 同（STM 固件不变）|
| ⑦ STM → PC | UART3 0x81 帧 | 同 |
| ⑧ PC 显示 | 计划中的 Qt 程序 | **`tools/tygd31_viewer/` Python Qt5** |

### 4.2 关键参数对照

| 参数 | 原 TYGD31 | 当前 nrf_tx | 备注 |
|---|---|---|---|
| 采集周期 | 500 ms | `IMAGE_PERIOD_MS = 500` | 同 |
| 单包发送间隔 | 1 ms | `IMAGE_FRAGMENT_GAP_MS = 1` | 同 |
| 重复发送块 | 0（每块 1 遍） | `IMAGE_REPEAT_SEND_ENABLED = 0` | 同 |
| 重发次数 | 0 | `IMAGE_MAX_RETRIES = 1` | **当前默认重试 1 次（原 0）** |
| ACK 超时 | 30 ms | `IMAGE_ACK_TIMEOUT_MS = 30` | 同 |
| 看门狗 | 3 s | `WATCHDOG_TIMEOUT_SECONDS = 3` | 同 |

---

## 5. 代码位置对照

### 5.1 RF 协议常量

| 常量 | 原位置 | 当前位置 |
|---|---|---|
| `PACKET_SIZE` / `RADIO_PACKET_SIZE` | `TYGD_TX/nRF/driver/drv_radio.h` | `legacy_protocol.h` |
| `PACKET_IMG_DATA_SIZE` | `drv_radio.h` | `drv_radio.h` / `legacy_protocol.h` 间接 |
| `CMD_IMG_*` / `CMD_*_CAPSULE_*` | `common/protocol.h` | `legacy_protocol.h` |
| `FRAME_DATA_SIZE` (20000) | `common/define.h` | `legacy_protocol.h::LEGACY_IMAGE_MAX_SIZE` |
| `DEVICE_ID_LEN` | 隐式 8 | `legacy_protocol.h::LEGACY_CAPSULE_SN_SIZE` |

### 5.2 图像处理逻辑

| 功能 | 原位置 | 当前位置 |
|---|---|---|
| 图像采集任务 | `image.c::image_capture_task` | `image.c::image_capture_task` |
| 分片发送 | `image.c::image_data_pkt_send` | `image.c::image_tx_service` |
| BEGIN 包组装 | `image.c::image_begin_pkt_send` | `image.c::image_tx_service`（内联）|
| END 包组装 | `image.c::image_end_pkt_send` | `image.c::image_tx_service`（内联）|
| 重复发送策略 | 无 | `IMAGE_BLOCK_PASSES` + `IMAGE_REPEAT_SEND_ENABLED` |
| ACK 超时重发 | 无（原 ACK 流程未实现） | `image_ack_service` |
| 图像 ID 递增 | `image_id = (image_id++) % 255` | `frame_id = (frame_id + 1u) % 255u` |

### 5.3 胶囊序列号管理

| 功能 | 原位置 | 当前位置 |
|---|---|---|
| SN 全局变量 | `capsule_sn_out`（UINT8* 指针） | `g_capsule_sn[8]`（数组）|
| SN 存储接口 | 内联在 `capsule_sn_set_checking` | **`capsule_sn_storage.h/c`**（独立模块）|
| 读 FICR DEVICEID | `DEVICE_ID_ADDR` 强转访问 | `FICR_DEVICE_ID_ADDR` + `capsule_sn_init` → 现在用 `capsule_sn_storage_init` |
| Flash 读写 | `drv_flash.h/c`（自实现）| **`drv_flash.h/c`**（基于 `nrf_nvmc`）|
| 设定命令监听 | `capsule_sn_set_checking`（启用）| **`capsule_sn_storage_set_checking`**（TODO 占位）|

### 5.4 STM 主控（未改动）

| 功能 | 位置 |
|---|---|
| UART3 协议 | `drv_usart.c::USART3_Send` |
| SD 卡存储 | `storage_card.c::SD_Card_storage_imgdata` |
| 文件头初始化 | `storage_card.c::SD_Card_write_file_header` |
| 按 SN 命名文件 | `storage_card.c::SD_Card_Open_Capsule_File`（文件名 `Y<SN>.YS`）|
| DeviceInfo 写入 | `storage_card.c::SD_Card_storage_imgdata` + `device_info.c` |
| 序列号转发 | `usart_transfer_station.c::capsule_sn_CMDDeal` |
| 主循环 | `do_main.c::domain` |

### 5.5 上位机（Python + Qt5）

| 功能 | 位置 |
|---|---|
| UART3 帧解析 | `tools/tygd31_viewer/frame_parser.py` |
| .YS 文件解析 | `tools/tygd31_viewer/frame_parser.py::parse_ys_file` |
| 串口实时显示 | `tools/tygd31_viewer/serial_viewer.py` |
| U 盘文件浏览 | `tools/tygd31_viewer/file_viewer.py` |
| 应用入口 | `tools/tygd31_viewer/main.py` |

---

## 6. 关键差异与待办

### 6.1 已修复的不一致

| 问题 | 原 → 当前 | 状态 |
|---|---|---|
| BEGIN 包 byte 14-16 版本号 | 原有 → 修复前漏掉 → **已修复** | ✅ |
| DeviceInfo 版本号偏移 | 原 5-6 / 9-10 → Python 兼容两种 | ✅ |
| VERSION_TEST 偏移 | 原 61 / 63 → Python 兼容 | ✅ |
| .YS 帧 JPEG header / data 顺序 | 原 header 在前 data 在后 → Python 修正拼接 | ✅ |
| UART3 payload JPEG 顺序 | 原 data 在前 header 在后 → Python 重组 JPEG | ✅ |

### 6.2 行为差异（原 vs 当前）

| 行为 | 原 TYGD31 | 当前实现 | 备注 |
|---|---|---|---|
| ACK 重试 | 不重试 | 重试 1 次 | 当前默认更抗丢包 |
| ACK 超时阈值 | 30 ms | 30 ms | 同 |
| 重复发送块 | 0 遍 | 0 遍 | 同（可改为 1 提速抗丢包）|
| 文件名 SN 字节 | 7 字节（去 1 字节）| 7 字节 | 同 |
| 文件大小上限 | `0x75B81BC0` ≈ 1.97 GB | 沿用 | 同 |

### 6.3 当前未实现（TODO）

| 功能 | 位置 | 影响 |
|---|---|---|
| `capsule_sn_storage_set_checking` | `nrf_tx/capsule_sn_storage.c` | TX 无法远程接收绑定命令（当前仅支持出厂 ID） |
| UART2 透传 SN 命令 | `usart_transfer_station.c` | 仍由 STM 固件处理 |
| RSSI 报警 / 数据丢失蜂鸣 | `do_main.c` | 仍由 STM 固件处理 |
| RX ACK 链路高成功率 | `nrf_rx/image.c::receiver_send_image_ack` | TXEN wait 偏小，需排查 RF 链路 |

### 6.4 已知问题

- **ACK 丢包率 90%**：TX 日志显示 `TXEN wait=3152`（~150 µs），理论应 ~1 ms；可能 `EVENTS_DISABLED` 被提前置位或天线切换问题。详见 [TIMING_ANALYSIS.md](../nrf_tx/examples/peripheral/radio/TX/TIMING_ANALYSIS.md) 章节 13。

---

## 7. 字节序 / 字段格式速查

| 字段 | 字节序 | 字段长度 | 取值范围 |
|---|---|---|---|
| frame_id | 单字节 | 1 | 0~255 |
| capsule_sn | 按 NRF FICR 字节序（小端） | 8 | — |
| block_size / image_length | 大端（big-endian） | 2 | 0~65535 |
| fragment_count / packet_index | 大端 | 2 | 0~65535 |
| legacy_checksum | 单字节累加和 | 1 | 0~255 |
| VERSION_MAIN/SUB/TEST | 单字节 | 1 | 0~255 |

> 注：原 TYGD31 用 `(i/256, i%256)` 两个字节写大端包序号（`pkt_len = i/256; buff[pkt_len++] = i % 256`），等价于大端 16 位整数，与当前实现 `LegacyProtocol_PutU16Be` 一致。

---

## 8. 修订记录

| 版本 | 日期 | 说明 |
|---|---|---|
| v1.0 | 2026-09-08 | 初版，整理原 TYGD31 协议与当前实现对照 |

