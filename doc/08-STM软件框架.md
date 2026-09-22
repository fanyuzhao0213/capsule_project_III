# STM 软件框架

## 1. 职责

STM32F103RET6连接RX和PC，负责接收RX重组后的图片、补齐设备信息、生成可存储JPEG、写SD卡并向PC转发，同时处理绑定、RTC等控制链路。

## 2. 启动流程

1. HAL和72 MHz系统时钟初始化。
2. 初始化GPIO、DMA、IWDG、TIM1、USART1/2/3、SDIO和FatFs。
3. USART1以115200 bit/s输出日志。
4. USART2以1 Mbit/s连接RX。
5. USART3以1 Mbit/s连接PC侧。
6. UART2/3启动DMA循环接收。
7. TIM1提供1 ms超时检测。
8. 尝试挂载SD卡；失败时记录警告，系统继续通信。
9. 主循环解析UART并持续喂独立看门狗。

## 3. 数据路径

```text
RX UART帧
  → DMA环形接收
  → FF 55 12 34帧解析
  → 检查0x81长度与校验和
  → 读取128字节DeviceInfo和原始JPEG
  → 补充STM版本、芯片ID、运行时间、RTC、最终JPEG长度
  → 必要时为JPEG补标准DQT/DHT头
  ├─ 写入SD卡
  └─ 以5A 41 59 53帧转发PC
```

## 4. 主要模块

| 文件 | 作用 |
| --- | --- |
| `main.c` | 时钟、外设初始化和主循环 |
| `app_uart_rx.c` | USART2/3 DMA接收与帧超时处理 |
| `capsule_protocol.c` | 图片及控制协议解析、设备信息更新、PC转发 |
| `legacy_jpeg_header.c` | 旧JPEG标准头数据 |
| `sd_storage.c` | FatFs挂载、文件创建和连续写入 |
| `sd2058.c` | RTC访问 |
| `ring_buffer.c` | 通信缓冲 |

## 5. 与天线扫描的关系

天线切换和RSSI统计全部发生在RX。STM只接收RX写入DeviceInfo的结果，并继续补充自己的字段。PC看到的12路RSSI来自最近一次RX正式扫描，当前天线编号来自RX，STM不重新计算。

## 6. 工程入口

Keil工程为 `stm32F103RET6/stm_project/MDK-ARM/stm_project.uvprojx`。编译输出不纳入源码目录清理后的基线。
