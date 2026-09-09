# 胶囊系统整体流程框架

## 1. 系统组成与职责

```text
PC工具
  ↕ UART3，1 Mbps，ZAYS控制帧/图片帧
STM32F103
  ↕ UART2，1 Mbps，ZAYS控制帧或内部图片帧
NRF_RX（接收记录仪）
  ↕ 2.4 GHz，固定254字节Radio包
NRF_TX（胶囊摄像头）
  ↕ SPI/I2C
CX93510 + OV7676
```

- PC工具：发送配置命令、显示实时JPEG、读取`.YS`文件。
- STM32：处理RTC命令，透传其他控制命令，接收完整图片，补齐JPEG、更新128字节设备信息、存储SD卡并转发PC。
- NRF_RX：上电扫描12路天线、负责无线收发、RAM会话绑定、按SN过滤图片、重组图片并发送ACK。
- NRF_TX：上电配置SN、周期广播SN；正式模式下采集JPEG、分片发送并等待ACK。

## 2. 上电流程

### NRF_TX

1. 初始化时钟、SN存储、看门狗、摄像头和Radio。
2. 打开3秒SN配置窗口，LED快速闪烁。
3. 合法的`0x40/0x42/0x44/0x46`会刷新3秒无操作计时。
4. `0x46`写入Flash成功后立即结束配置窗口。
5. 配置成功后连续广播3次SN，之后每500ms广播一次。
6. `IMAGE_TRANSMISSION_ENABLED=0`时只广播SN；设为`1`后每500ms触发一次图片采集。

### NRF_RX

1. 上电绑定状态清空，默认为未绑定。
2. 依次扫描ANT1～ANT12，每路监听650ms。
3. 只统计CRC正确包，选择平均RSSI最强的天线；相同时比较CRC成功率。
4. 固定最优天线，进入UART、Radio和图片处理主循环。

### STM32

1. 初始化UART2/UART3 DMA、RTC、SD卡和协议解析器。
2. UART3接收PC命令；`0x02`本地设置RTC，其他ZAYS命令透传给NRF_RX。
3. UART2接收NRF_RX数据；控制应答透传PC，完整图片进入校验、存储和PC转发流程。

## 3. 控制协议路由

| 命令 | 处理位置 | 说明 |
| --- | --- | --- |
| `0x02` | STM32 | RTC时间设置 |
| `0x20/0x21` | NRF_RX | 查询本次上电会话绑定SN |
| `0x22/0x23` | NRF_RX | 解除RAM绑定 |
| `0x24/0x25` | NRF_RX | 设置RAM绑定SN |
| `0x40/0x41` | NRF_TX | 出厂SN配置准备 |
| `0x42/0x43` | NRF_TX | 查询DEVICEID |
| `0x44/0x45` | NRF_TX | 暂存新SN并回显 |
| `0x46/0x47` | NRF_TX | 确认写入SN Flash |
| `0x49` | NRF_TX/NRF_RX | 控制协议错误 |
| `0x05` | NRF_TX→NRF_RX→PC | SN广播 |

STM32除`0x02`外不解释控制业务；NRF_RX截获`0x20～0x25`，只将`0x40～0x47`通过Radio发送给NRF_TX。

## 4. 多胶囊绑定与过滤

```text
未绑定：接收并上报所有SN广播，丢弃所有图片
   ↓ PC发送0x24绑定8字节SN
已绑定：只接受匹配SN的广播和图片，忽略其他胶囊
   ↓ PC发送0x22或RX复位/断电
未绑定
```

绑定只保存在NRF_RX RAM，不写Flash。这样每次开机都由PC重新选择胶囊，不会因上次绑定造成设备不可发现。

## 5. 图片传输流程

```text
NRF_TX采集完整JPEG
  → 0x01 BEGIN（帧ID、SN、长度、分片数、版本）重复2次
  → 0x80 DATA（每片最多242字节）
  → 0x03 END（整图累加校验）
NRF_RX按绑定SN过滤并重组
  → 校验成功后0x10 ACK重复2次
  → FF 55 12 34 + 0x81 + DeviceInfo + JPEG 发给STM32
STM32校验并生成完整JPEG
  → 写入.YS记录
  → ZAYS + 0x81 + 完整JPEG + DeviceInfo 发给PC
PC工具
  → 校验数据区
  → 提取JPEG并显示
```

## 6. 可靠性机制

- Radio硬件使用16位CRC。
- 短控制请求和应答各重复发送3次，接收端忽略重复应答。
- BEGIN重复2次；DATA带分片编号；END带整图累加校验。
- 图片完成ACK重复2次，TX超时后按配置执行重传。
- RX Radio队列与UART发送均在主循环异步处理，避免中断中执行长任务。
- RX UART以最后一个字节后100ms静默作为短控制包结束条件。

## 7. 调试开关

TX配置集中在`nrf_tx/examples/peripheral/radio/TX/config.h`：

- `TX_CONFIG_LOG_ENABLED`：SN配置日志。
- `TX_INIT_LOG_ENABLED`：摄像头/SPI初始化日志。
- `IMAGE_TRANSMISSION_ENABLED`：图片采集和发送总开关。

RX配置集中在`nrf_rx/examples/peripheral/radio/receiver/config.h`：

- `RF1662_STARTUP_SCAN_ENABLED`：12路天线上电扫描。
- `RF1662_SCAN_DWELL_MS`：每路扫描时长。
- `UART_RX_IDLE_TIMEOUT_US`：UART短包静默结束时间。

## 8. 当前已清理内容

- TX已删除运行阶段绑定/解绑协议，只保留出厂SN配置接口。
- 删除未使用的`0x07`胶囊ID广播定义。
- 删除TX未使用的Flash解绑接口、Radio诊断计数和固定SN测试代码。
- TX常规图片、广播和初始化日志默认关闭，只保留可配置的SN配置日志。
- PC实时工具已切换到当前ZAYS帧头和“完整JPEG + DeviceInfo”布局。
