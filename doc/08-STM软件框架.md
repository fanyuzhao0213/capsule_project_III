# STM 软件框架与完整运行流程

本文按 `stm32F103RET6/stm_project/Core/Src` 当前源码说明STM32F103RET6工程。STM位于RX与PC/SD卡之间，不参与无线收包和天线选择，主要负责串口接收、图片校验与重建、设备信息补齐、SD记录以及PC转发。

## 1. STM职责与数据方向

```text
胶囊TX ──2.4 GHz──> nRF RX
                       │
                       │ USART2，1 Mbit/s
                       ▼
                    STM32F103RET6
                       ├── SDIO/FatFs ──> SD卡Y<SN>.YS
                       │
                       └── USART3，921600 bit/s ──> PC

PC控制命令 ──USART3──> STM
                       ├── CMD 0x02：本地写SD2058 RTC
                       └── 其他命令：USART2透明转发nRF RX
```

STM不重新计算天线或RSSI。RX已经把当前天线和12路RSSI写入128字节DeviceInfo，STM保留这些字段，只补写自己负责的版本、芯片ID、运行时间、RTC和最终JPEG长度。

## 2. 关键配置

| 项目 | 当前源码值 | 位置 | 作用与注意事项 |
| --- | ---: | --- | --- |
| 系统时钟 | 72 MHz | `main.c` | HSE 8 MHz经PLL×9；APB1=36 MHz，APB2=72 MHz |
| USART1日志 | 1 Mbit/s | `usart.c` | 当前源码不是旧注释中的115200 bit/s |
| USART2 RX链路 | 1 Mbit/s | `usart.c` | 连接nRF RX；RX使用DMA循环模式 |
| USART3 PC链路 | 921600 bit/s | `usart.c`、`stm_project.ioc` | RX使用循环DMA，图片TX使用普通DMA；源码与CubeMX配置保持一致 |
| UART DMA底层缓冲 | 每路1024 B | `app_uart_rx.c` | IDLE、DMA半满或全满事件提交新数据 |
| USART2软件ring | 8192 B | `app_uart_rx.c` | 吸收图片突发和SD阻塞期间的数据 |
| USART3软件ring | 1024 B | `app_uart_rx.c` | PC控制命令数据量较小 |
| 主循环单次消费 | 256 B/路 | `app_uart_rx.c` | 每轮USART2和USART3各最多读取一块 |
| TIM1时基 | 1 ms | `tim.c` | 72 MHz/72/1000；当前只用于运行时间与帧率统计 |
| 阻塞UART超时 | 1000 ms | `capsule_protocol.c` | 仅用于控制帧阻塞转发，不是图片解析超时 |
| IWDG | 预分频32，重装4095 | `iwdg.c` | 按40 kHz典型LSI约3.28 s，实际随LSI误差变化 |
| RX图片最大原始JPEG | 20000 B | `capsule_protocol.c` | payload还包含128 B DeviceInfo |
| 单条.YS记录 | 20064 B | `capsule_protocol.c`/`sd_storage.c` | 128 B DeviceInfo + 最终JPEG + 尾部零填充 |
| 记录JPEG容量 | 19936 B | `capsule_protocol.c` | 20064−128；补标准头后的JPEG也不能超过此值 |
| SD同步周期 | 每10帧 | `capsule_protocol.c` | 第10帧写完调用`f_sync()`；掉电可能丢失尚未同步的缓存记录 |

## 3. 上电初始化流程

```text
HAL_Init()
  ↓
SystemClock_Config()：HSE 8 MHz × 9 = 72 MHz
  ↓
GPIO → DMA → IWDG → TIM1 → USART1/2/3 → SDIO → FatFs
  ↓
AppLog_Init()：USART1日志
  ↓
AppUartRx_Init()
  ├─ CapsuleProtocol_Init()
  │    ├─ 清空图片/控制解析状态
  │    └─ 初始化PB6/PB7软件I2C并探测SD2058
  ├─ USART2启动1024 B循环DMA + 8 KB ring
  └─ USART3启动1024 B循环DMA + 1 KB ring
  ↓
启动TIM1 1 ms中断
  ↓
SdStorage_Init()挂载SD卡并打印容量
  ├─ 成功：允许保存图片
  └─ 失败：仅警告，UART通信继续运行
  ↓
进入永久主循环
```

## 4. 主循环与中断分工

### 4.1 主循环

```text
AppUartRx_Process()
  ├─ USART2 ring取最多256 B → CapsuleProtocol_InputFromNrf()
  ├─ USART3 ring取最多256 B → CapsuleProtocol_InputFromPc()
  └─ CapsuleProtocol_Process()处理DMA事件和等待中的控制应答
  ↓
HAL_IWDG_Refresh()
  ↓
立即进入下一轮
```

当前主循环没有 `WFI/WFE`，会持续轮询。SD/FatFs写入和部分短控制帧转发是主循环中的阻塞操作；USART3图片转发使用DMA，不阻塞主循环。

### 4.2 中断只做搬运和状态推进

| 中断/回调 | 工作 |
| --- | --- |
| USART2/3 IDLE、DMA HT/TC | 把DMA新增区间复制到各自ring，不解析协议 |
| TIM1 1 ms | `application_milliseconds++` |
| USART3 TX DMA完成 | 按Header→JPEG→DeviceInfo→Checksum推进下一段 |
| SDIO/DMA中断 | 交给HAL SD驱动处理 |

中断中不打印业务日志、不写SD卡、不进行整帧校验，避免长时间占用中断。

## 5. 双UART DMA接收流程

每路UART都有两层缓冲：

```text
UART字节
  ↓ DMA循环写
1024 B dma_buffer
  ↓ IDLE/半满/满回调计算新增区间
软件RingBuffer
  ↓ 主循环每轮读取最多256 B
协议解析入口
```

DMA位置可能从数组末尾回绕到开头，因此提交函数分两段复制：`[last, dma_size)`和`[0, current)`。ring空间不足时，中断只累计 `lost_bytes`；主循环下次消费时再打印丢字节警告。

## 6. nRF RX到STM的图片解析

RX输入格式：

```text
FF 55 12 34 81
PayloadLength_BE（2 B）
DeviceInfo（128 B）
Raw JPEG（0～20000 B）
Checksum（1 B）
```

流式状态机：

```text
PREAMBLE：逐字节寻找FF 55 12 34 81
  ↓
LENGTH：读取2字节payload长度
  ├─ 小于128或大于20128：拒绝并回到PREAMBLE
  └─ 合法：进入BODY
  ↓
BODY：持续接收DeviceInfo + JPEG + Checksum
  ↓
完整后调用CapsuleProtocol_HandleFrame()
  ↓
无论成功或拒绝都重置解析器，等待下一帧
```

解析器支持任意DMA分段边界。但当前代码没有图片帧静默超时：如果收到合法帧头和长度后数据永久中断，解析器会继续等待BODY，直到凑够声明长度；TIM1的1 ms计数目前没有用于重置该状态。

## 7. 完整图片处理

```text
检查payload和原始JPEG长度
  ↓
计算DeviceInfo+JPEG的8位累加和
  ↓
检查接收Checksum
  ↓
检查DeviceInfo前5字节标识00 55 AA 88 99
  ↓
上一帧USART3图片DMA是否仍忙？
  ├─ 忙：整帧丢弃，避免覆盖DMA正在读取的legacy_record
  └─ 空闲：继续
  ↓
清空20064 B legacy_record并复制128 B DeviceInfo
  ↓
检查JPEG是否包含DQT和DHT
  ├─ 已包含：原样复制
  └─ 缺少：写入标准JPEG头，再拼接原图去掉SOI后的内容
  ↓
补写STM版本、芯片ID、运行时间、RTC和最终JPEG长度
  ├─ 启动USART3四段DMA转发PC
  └─ 追加20064 B固定记录到SD卡
```

`legacy_record`既是SD写入源，也是USART3 DMA图片源。在四段DMA全部结束前不能修改，因此如果下一张完整图片到达过快，会被明确丢弃而不是覆盖上一张。

## 8. PC图片转发状态机

STM转发给PC的顺序与SD记录顺序不同：

```text
ZAYS + 0x81 + PayloadLength
  ↓ DMA完成
JPEG
  ↓ DMA完成
128 B DeviceInfo
  ↓ DMA完成
Checksum
  ↓ DMA完成
PC_TX_IDLE
```

校验和覆盖“JPEG + DeviceInfo”。DMA回调只启动下一段；完成/错误日志延迟到主循环输出。

## 9. PC与nRF控制链路

### 9.1 nRF控制应答到PC

USART2输入处于图片帧头搜索状态时，会同时识别 `5A 41 59 53` 控制帧。完整帧通过校验后放入一个32字节pending缓冲。USART3图片DMA空闲后，主循环再把该控制应答发给PC。

pending缓冲只有一帧容量；如果PC图片DMA繁忙期间连续收到多个控制应答，后到的应答可能覆盖前一帧。

### 9.2 PC命令到nRF或RTC

```text
USART3收到数据
  ↓
是否为完整ZAYS控制帧？
  ├─ 否：普通字节或不完整匹配片段直接转发USART2
  └─ 是：校验长度和checksum
         ├─ CMD=0x02、payload=7 B：STM本地写SD2058
         └─ 其他CMD：完整帧阻塞转发USART2给nRF RX
```

PC的0x02时间顺序为“年、月、日、星期、时、分、秒”；SD2058寄存器顺序相反，写入函数会完成转换并操作RTC控制寄存器的写使能位。

## 10. SD卡.YS文件

文件名由完整8字节胶囊SN生成：

```text
Y<16个十六进制SN字符>.YS
```

文件没有全局头，由连续固定长度记录组成：

```text
每条20064 B
├─ 0～127：DeviceInfo
└─ 128～20063：完整JPEG及尾部零填充
```

打开已有文件时，如果长度不是20064的整数倍，说明末尾可能是上次掉电留下的不完整记录。代码会截断到最近的完整记录边界、立即同步，再从文件末尾继续追加。

发生FatFs/SDIO传输错误时，代码会放弃旧FIL对象、卸载卷、执行HAL SD Abort、重新初始化SD卡并重新挂载。下一帧重新打开文件。

## 11. 主要模块阅读顺序

建议按调用链阅读：

1. `main.c`：初始化顺序和永久主循环。
2. `app_uart_rx.c`：DMA事件如何进入ring、主循环如何取数据。
3. `capsule_protocol.c`：图片/控制两套状态机和PC DMA发送链。
4. `sd_storage.c`：固定记录、文件复用、尾记录修复和错误恢复。
5. `sd2058.c`：软件I2C和RTC时间顺序转换。
6. `ring_buffer.c`：中断写、主循环读的基础缓冲。
7. `usart.c`、`tim.c`、`iwdg.c`、`sdio.c`：底层外设参数。

Keil工程入口为 `stm32F103RET6/stm_project/MDK-ARM/stm_project.uvprojx`。
