# nRF TX/RX 工作流程与源码对照说明

本文对应当前工作区中的两个工程：

- TX：`nrf_tx/examples/peripheral/radio/TX/main.c`
- RX：`nrf_rx/examples/peripheral/radio/receiver/main.c`
- 公共协议：两个工程各自目录中的 `legacy_protocol.h`

## 1. 整体数据流

```text
OV7676
  │ 并行图像数据
  ▼
CX93510 ── JPEG压缩/帧缓冲
  │ SPI读取JPEG
  ▼
nRF TX ── BEGIN×2 → DATA[0..N-1] → END ──► nRF RX
  ▲                                               │
  └──────────────── ACK×2 ◄───────────────────────┤
                                                  │ 重组、完整性检查
                                                  ▼
                                        STM帧缓冲（约图像大小+136字节）
                                                  │ 1 Mbps UART，非阻塞分批发送
                                                  ▼
                                                STM32
```

TX 和 RX 都直接操作 `NRF_RADIO` 寄存器，不使用 SoftDevice。日志使用 RTT。

## 2. 无线链路必须一致的参数

TX 的 `radio_configure_image_link()` 和 RX 的 `receiver_radio_init()` 必须保持一致。

| 参数 | 当前值 | 修改要求 |
|---|---:|---|
| 频率 | `FREQUENCY=0`，即 2400 MHz | TX、RX同时修改 |
| 模式 | `Nrf_2Mbit` | TX、RX同时修改 |
| 固定包长 | 254字节 | TX、RX、协议头同时修改 |
| 地址 | `PREFIX0/BASE0`等当前寄存器值 | TX、RX同时修改 |
| 字节序 | Radio big endian | TX、RX同时修改 |
| 硬件CRC | 16位 | TX、RX同时修改 |
| CRC初值 | `0xFFFF` | TX、RX同时修改 |
| CRC多项式 | `0x11021` | TX、RX同时修改 |

特别注意：源码部分旧注释曾写“1 Mbit”，但当前寄存器实际配置是 **2 Mbit**，分析和调试以寄存器赋值为准。

## 3. 当前无线应用协议

### 3.1 BEGIN 开始包

TX 连续发送两次完全相同的 BEGIN，用来降低开始包丢失概率。

| 字节位置 | 长度 | 内容 |
|---|---:|---|
| 0 | 1 | `0x01`，图片开始命令 |
| 1 | 1 | 图片ID |
| 2～9 | 8 | 胶囊序列号 |
| 10～11 | 2 | JPEG总长度，大端 |
| 12～13 | 2 | DATA包总数，大端 |
| 14～253 | 240 | 0填充 |

RX 收到第二个 BEGIN 时会再次清空同一张图片的重组状态。因为两个 BEGIN 是连续发送且后面才开始 DATA，所以不会擦掉有效数据。

### 3.2 DATA 数据包

| 字节位置 | 长度 | 内容 |
|---|---:|---|
| 0 | 1 | `0x80`，图片数据命令 |
| 1 | 1 | 图片ID |
| 2～9 | 8 | 胶囊序列号 |
| 10～11 | 2 | 分包序号，大端，从0开始 |
| 12～253 | 最多242 | JPEG数据；最后一包不足部分保持0 |

分包数量为：

```text
packet_count = (jpeg_length + 242 - 1) / 242
```

例如 JPEG 长度 16834 字节：

```text
packet_count = ceil(16834 / 242) = 70包
```

### 3.3 END 结束包

| 字节位置 | 长度 | 内容 |
|---|---:|---|
| 0 | 1 | `0x03`，图片结束命令 |
| 1 | 1 | 图片ID |
| 2～9 | 8 | 胶囊序列号 |
| 10 | 1 | 整张JPEG的8位累加和 |
| 11～253 | 243 | 0填充 |

8位累加和计算方式：

```c
checksum = (uint8_t)(checksum + jpeg_byte);
```

从JPEG第一个字节一直加到最后一个字节，每次只保留低8位。这个应用层校验和每张图片只有一个，放在 END 包中，不是每个 DATA 包各带一个。

### 3.4 ACK 应答包

| 字节位置 | 长度 | 内容 |
|---|---:|---|
| 0 | 1 | `0x10`，图片接收成功 |
| 1 | 1 | 图片ID |
| 2～9 | 8 | 胶囊序列号 |

RX 只有在分包数量完整且整图8位校验和正确时才发送 ACK。ACK 连续发送两次。

### 3.5 两层校验的区别

- Radio硬件CRC16：每个 BEGIN、DATA、END、ACK 包都由硬件独立检查。失败包不会进入软件队列。
- 应用层8位累加和：覆盖整张JPEG，用来判断所有DATA组合后的图片内容是否正确。

## 4. TX 工作流程

### 4.1 上电初始化

入口是 TX `main()`，顺序如下：

1. `clock_init()`：启动16 MHz外部高频晶振。
2. `NRF_LOG_INIT()`：初始化RTT日志。
3. `capsule_sn_init()`：读取FICR唯一ID。
4. `watchdog_init()`：启动3秒硬件看门狗，并检查上一次是否因看门狗超时复位。
5. `image_tx_led_init()`：P0.08配置为发送指示灯。
6. `cx93510_init()`：初始化SPI、CX93510和JPEG部分。
7. `ov7676_init()`：通过CX93510代理接口初始化OV7676。
8. `radio_configure_image_link()`：配置无线收发参数并进入RX。
9. `timer_init()`：TIMER1每1 ms产生一次中断。
10. `m_capture_due=true`：上电后立即请求第一张图片。

### 4.2 胶囊序列号

`capsule_sn_init()`读取：

```c
NRF_FICR->DEVICEID[0]
NRF_FICR->DEVICEID[1]
```

按Cortex-M4小端内存顺序组成8字节：先放`DEVICEID[0]`低字节到高字节，再放`DEVICEID[1]`低字节到高字节。BEGIN、DATA、END和ACK匹配全部使用相同序列号。

当前尚未实现原工程“Flash设置序列号覆盖FICR默认值”的持久化设置流程。

### 4.3 500 ms采集调度

`TIMER1_IRQHandler()`每1 ms执行一次，只完成两件事：

- `m_time_ms++`，形成软件毫秒时钟。
- 每累计`IMAGE_PERIOD_MS=500`设置`m_capture_due=true`。

中断中不采图、不读SPI、不发完整图片，避免长时间阻塞Radio中断。

`image_capture_task()`在主循环检查采集标志：

- 没有请求：立即返回。
- 上一张还在发送或等待ACK：打印`Image period skipped`并跳过。
- 空闲：调用`cx93510_capture_one()`采集一帧。
- JPEG长度必须在1～20000字节之间。
- 成功后图片ID加1，初始化发送状态并点亮P0.08。

### 4.4 TX发送状态结构 `m_image_tx`

最关键字段：

| 字段 | 含义 |
|---|---|
| `active` | 还有DATA分包未发送 |
| `awaiting_ack` | END已发送，等待RX确认 |
| `legacy_send_begin` | 本轮发送前是否要发BEGIN×2 |
| `frame_id` | 当前图片ID |
| `fragment_index` | 下一包DATA的序号 |
| `fragment_count` | 当前图片总包数 |
| `block_offset` | JPEG在CX93510帧缓冲中的地址 |
| `block_size` | JPEG总长度 |
| `block_sent` | 已从帧缓冲读取并发送的字节数 |
| `next_fragment_ms` | 下一分包允许发送的时间 |
| `legacy_checksum` | 已发送JPEG数据的8位累加和 |
| `retry_count` | 当前重发次数 |
| `ack_deadline_ms` | 首次发送后的ACK截止时间 |

### 4.5 每次主循环只发送一个DATA包

`image_tx_service()`不是一次把整张图发完，而是每次调用最多发送一个DATA包：

1. 检查`active`和`next_fragment_ms`。
2. 首次发送第0包之前发送BEGIN两次。
3. 从CX93510读取最多242字节。
4. 把这些字节累加到`legacy_checksum`。
5. 调用`radio_send_packet()`发送固定254字节。
6. 更新`block_sent`和`fragment_index`。
7. 设置下一包时间为当前时间加1 ms。
8. 所有DATA完成后发送END，END[10]放整图校验和。

`radio_send_packet()`本身会短暂执行：RX关闭 → 设置TX缓冲 → 发送一包 → 恢复RX。它只阻塞一包的发送时间，不阻塞整张图片。

### 4.6 ACK和重发

首次发送完成：

```text
BEGIN×2 → DATA 0...N-1 → END → 等待ACK 30 ms
```

`radio_rx_process()`检查ACK必须同时满足：

- 命令为`0x10`。
- 图片ID与当前图片相同。
- 8字节序列号完全相同。

收到有效ACK后结束本张图片。

30 ms没有ACK时，`image_ack_service()`按原工程逻辑只重发一次：

```text
DATA 0...N-1 → END
```

重发不再发送BEGIN，重发结束后也不再等待第二个ACK。这一点很难理解，但它是当前为了保持原工程行为而保留的逻辑。

因此RX在第一次END发现缺包时不能清空接收状态，必须继续等待重发DATA补齐缺失分包。

### 4.7 TX主循环

```c
while (true)
{
    radio_rx_process();   // 处理ACK
    image_ack_service();  // 检查30 ms超时，必要时启动重发
    image_capture_task(); // 处理500 ms采集请求
    image_tx_service();   // 每轮最多发送一个DATA包
    NRF_LOG_PROCESS();    // 输出RTT日志
    __WFE();              // 等待下一次中断
}
```

## 5. RX 工作流程

### 5.1 RX当前有效代码范围

RX `main.c`目前仍保留角色条件编译：

```c
#if APP_ROLE_DEDICATED_RECEIVER
    // 当前真正参与RX编译的代码
#else
    // 历史遗留的摄像头TX代码，不参与当前RX固件
#endif
```

`app_role.h`在RX工程中把`APP_ROLE_DEDICATED_RECEIVER`设为1。阅读RX时只需要看文件前半部分；后半部分TX分支是冗余代码，后续可以像TX工程一样物理删除。

### 5.2 RX上电初始化

RX `main()`顺序如下：

1. `receiver_clock_init()`：启动高频晶振。
2. 初始化RTT日志。
3. `receiver_mode_pin_init()`：P0.23输出低电平，使外部射频电路进入接收状态。
4. `receiver_uart_idle_timer_init()`：配置UART RX的500 ms静默检测。
5. `receiver_uart_init()`：1 Mbps，RX=P0.16，TX=P0.15。
6. 可选发送一次UART启动测试串。
7. `receiver_radio_init()`：配置Radio并开始收包。

### 5.3 P0.23收发控制

- 正常接收：P0.23保持低电平。
- 需要回ACK：先关闭Radio接收和中断，再把P0.23拉高，发送ACK两次。
- ACK完成：P0.23拉低，重新设置接收缓冲并恢复Radio中断。

如果P0.23没有正确进入接收状态，`radio_rx`不会持续增加。当前日志`radio_rx`连续增加且`dropped=0`，说明该管脚和Radio接收工作正常。

### 5.4 Radio中断与环形队列

`RADIO_IRQHandler()`只做快速工作：

1. 清除END事件。
2. 用`CRCSTATUS`检查当前包硬件CRC。
3. CRC正确时，把固定254字节复制到`m_receiver_queue`。
4. 队列满时`dropped++`。
5. 立即调用`receiver_radio_arm()`接收下一包。

耗时的图片解析、校验、UART发送和日志均不放在中断中。

`RADIO_QUEUE_DEPTH=8`使用“保留一个空槽”区分队列满和空，因此实际最多缓存7包。不要简单理解成能缓存8包。

### 5.5 图片重组状态 `m_legacy_image_rx`

| 字段 | 含义 |
|---|---|
| `active` | 已收到有效BEGIN，当前正在重组图片 |
| `image_id` | 当前图片ID |
| `capsule_sn[8]` | 当前图片对应的胶囊序列号 |
| `image_length` | JPEG总长度 |
| `packet_count` | 应收到的DATA总包数 |
| `received_count` | 已收到的不同DATA数量 |
| `received_map[]` | 每个分包是否已收到，用于去重和补包 |
| `image[]` | 最终重组的JPEG数据，最大20000字节 |

### 5.6 BEGIN处理

`receiver_process_legacy_packet()`收到`0x01`时：

1. 读取大端JPEG长度和包数。
2. 检查长度不超过20000字节。
3. 检查包数等于`ceil(length/242)`。
4. 清空旧重组状态和`received_map`。
5. 保存图片ID、序列号、长度和包数。
6. 设置`active=true`。

### 5.7 DATA处理、去重和补包

收到`0x80`前必须满足：

- `active=true`。
- 图片ID相同。
- 8字节序列号相同。

随后读取分包序号：

- 序号越界：丢弃。
- `received_map[index]`已经是1：说明是重复包，直接忽略。
- 新包：复制到`image[index*242]`，置位map并增加`received_count`。

这套map机制使重发非常重要：第一次遗漏的包可以由第二遍DATA补齐，已经收到的重复包不会重复计数或覆盖。

### 5.8 END处理

收到`0x03`后按顺序检查：

1. `received_count == packet_count`。
2. 对重组JPEG重新计算8位累加和。
3. 计算结果等于END[10]。

如果分包不完整，RX打印：

```text
Legacy image incomplete: id=x received=a/b
```

此时不清空`active`，等待TX的DATA+END重发继续补包。

完整且校验正确时：

1. `receiver_send_image_ack()`发送ACK两次。
2. `receiver_forward_complete_image()`封装STM帧。
3. 设置异步UART发送状态。
4. 清除本张图片的Radio重组状态。

### 5.9 STM帧格式

完整图片被封装为：

| 位置 | 内容 |
|---|---|
| 0～3 | 固定头`FF 55 12 34` |
| 4 | `0x81`，图片转发命令 |
| 5～6 | payload长度，大端 |
| 7～134 | 128字节设备信息 |
| 设备信息内偏移11～18 | 8字节胶囊序列号 |
| 135开始 | JPEG数据 |
| 最后1字节 | JPEG 8位累加和 |

总帧长度为：

```text
7 + 128 + jpeg_length + 1
```

### 5.10 为什么UART必须异步发送

1 Mbps、8N1实际每字节需要10 bit。发送约16 KB至少需要约160 ms。若主循环在这里一直等待，Radio中断虽然还能收包，但7包有效队列会迅速填满。

当前方案：

- `receiver_forward_complete_image()`只负责封装并设置pending状态。
- `receiver_uart_tx_service()`每轮最多尝试填入64字节。
- UART FIFO满时立即返回主循环，不忙等。
- 下一轮先继续处理Radio包，再继续填UART。

日志含义：

```text
Legacy image queued for STM
```

表示完整STM帧已经在RAM中等待/正在发送。

```text
Legacy image forwarded to STM
```

表示这帧的所有字节已经交给UART FIFO/驱动。物理线路可能仍有少量FIFO字节尚未移出。

```text
stm_pending=1
```

表示上一帧仍占用`m_stm_frame`；`0`表示该缓冲已释放。

### 5.11 RX主循环

```c
while (true)
{
    receiver_uart_process_received(); // 收集STM发回的数据，500 ms静默后用RTT打印
    receiver_forward_one();           // 每轮从Radio队列解析一包
    receiver_uart_tx_service();       // 每轮最多向UART FIFO填64字节
    NRF_LOG_PROCESS();                // 处理一条RTT日志
    __WFE();                          // 无工作时等待中断
}
```

这个顺序的重点是：Radio包解析和UART发送交替推进，任何一方都不能长时间堵住主循环。

## 6. 日志快速判断表

| 日志 | 含义 | 是否正常 |
|---|---|---|
| `Legacy image begin`连续两次 | TX发送BEGIN×2 | 正常 |
| `radio_rx == radio_processed` | 接收和主循环处理完全跟上 | 最理想 |
| 两者短暂相差1 | 一包刚进入中断队列 | 正常 |
| `dropped`持续增加 | Radio软件队列溢出 | 异常 |
| `Legacy image incomplete` | END到达时仍缺DATA | 无线丢包或队列溢出 |
| `checksum failed` | 分包齐但整图内容不一致 | 异常 |
| `queued for STM` | 图片已进入STM发送缓冲 | 正常 |
| `forwarded to STM` | 整帧已交给UART驱动 | 正常 |
| `STM UART busy` | 上一帧未释放，当前完整图无法排队 | 需要降低帧率或提高下游吞吐量 |
| TX `acknowledged` | RX完整接收并回ACK | 正常 |
| TX `retry 1/1` | 30 ms内未收到有效ACK | 检查RX完整性、ACK和射频切换 |

## 7. 容易安全修改的参数

### 7.1 TX采集周期

位置：TX `main.c`顶部。

```c
#define IMAGE_PERIOD_MS 500u
```

数值增大：帧率降低，下游更容易跟上。数值减小：帧率提高，但更容易出现`STM UART busy`。

### 7.2 TX分包间隔

```c
#define IMAGE_FRAGMENT_GAP_MS 1u
```

增大可降低瞬时接收压力，但整张发送更慢。减为0会连续发送，RX主循环和UART压力显著增加。

### 7.3 TX ACK等待时间

```c
#define IMAGE_ACK_TIMEOUT_MS 30u
```

只影响首次发送后等待ACK的时间。过小可能在RX刚完成校验、切换到TX时提前触发重发；过大会降低无ACK时的帧率。

### 7.4 RX UART单轮填充预算

```c
#define UART_TX_SERVICE_BUDGET 64u
```

增大：UART FIFO填得更积极，但一次主循环占用更久。减小：Radio处理延迟更低，但函数调用次数更多。当前64是保守值。

### 7.5 RX启动测试串

```c
#define UART_BRIDGE_STARTUP_TEST 1u
```

量产时可改为0，避免上电向STM发送ASCII测试文字。

### 7.6 指示灯和模式脚

- TX发送灯：`IMAGE_TX_LED_PIN=8`。
- RX外部收发控制：`RECEIVER_MODE_PIN=23`。
- RX UART：RX=P0.16、TX=P0.15。

改管脚前必须核对原理图，尤其P0.15/P0.16同时可能与其他外设复用。

### 7.7 TX硬件看门狗

TX在日志初始化后启动nRF52832硬件看门狗，默认超时为3秒：

```c
#define WATCHDOG_TIMEOUT_SECONDS 3u
```

- 主循环每完成一次调度就通过`RR[0]`喂狗。
- CPU执行`WFE`正常休眠时，看门狗仍继续计时；1 ms定时中断会唤醒主循环并及时喂狗。
- 调试器断点暂停CPU时，看门狗暂停，便于开发阶段单步调试。
- 摄像头采集的正常最长等待为1秒，低于3秒超时，不会造成正常流程误复位。
- SPI等待、摄像头访问或程序主循环卡死后，程序无法继续喂狗；约3秒后芯片自动复位。
- 若上一次是看门狗超时复位，启动日志会出现`Previous reset was caused by watchdog timeout`。

修改超时时间时，必须保证它明显大于`IMAGE_CAPTURE_TIMEOUT_MS`以及一次正常外设操作的最长时间。不要在SPI或摄像头等待循环中无条件喂狗，否则硬件卡死时看门狗也无法恢复系统。

## 8. 修改时必须同步的内容

以下参数不能只改一边：

1. Radio频率、模式、地址、包长、字节序、CRC参数。
2. BEGIN/DATA/END/ACK命令值。
3. 序列号长度和字节顺序。
4. DATA头长度与每包有效载荷。
5. 大端长度、包数、包序号的解析规则。
6. STM帧头、设备信息长度和校验范围。
7. UART波特率、数据位、停止位和管脚连接。

## 9. 不建议轻易修改的部分

- `LEGACY_IMAGE_MAX_SIZE`：RX同时分配图片缓冲和STM帧缓冲，nRF52832 RAM已较紧张。
- `RADIO_QUEUE_DEPTH`：增加会按`254字节 × 增量`占用RAM；减小会降低抗主循环抖动能力。
- `UART_TX_FIFO_SIZE`：增加占RAM，减小会增加服务频率。
- BEGIN重复次数、ACK次数和“重发不带BEGIN”规则：TX/RX状态机强相关。
- P0.23切换时序：错误修改会导致ACK发不出去或恢复不了接收。
- 在Radio/UART中断中加入日志或大循环：非常容易造成丢包。

## 10. 当前建议的调试顺序

1. TX确认FICR序列号、JPEG长度和`period=500 ms`。
2. RX确认`radio_rx`持续增加。
3. 观察`radio_rx-radio_processed`是否很快归零。
4. 观察`dropped`是否始终为0。
5. 确认每张图都有`queued for STM`和`forwarded to STM`。
6. TX确认大多数图片出现`acknowledged`而不是`retry`。
7. STM端检查固定头、长度、JPEG累加和与文件写入结果。
