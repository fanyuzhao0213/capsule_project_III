# 磁控胶囊内窥镜通信项目文档

当前协议总览见根目录[磁控胶囊内窥镜系统通信协议-当前工程](../磁控胶囊内窥镜系统通信协议-当前工程.md)。根目录原有Excel表尚未同步新协议，按历史参考处理。

## 阶段基线

- 主工程保留 `nrf_tx`、`nrf_rx`、`stm32F103RET6` 三部分，同时保留 `TYGD31-2.4GHZ` 作为旧工程参考。
- 另有独立的 `nrf_tx_led_off` 测试工程：P0.08补光灯始终关闭，采图准备等待和无线协议不变；不要把它当作默认TX工程。
- 保留 `tools` 上位机工具；根目录原Excel协议表只供历史对照，当前报文以根目录Markdown和源码为准。
- 无线链路工作在2.4 GHz频段，当前固定中心频率为2400 MHz，即nRF52 `FREQUENCY=0`。
- TX 图片采集周期保持 500 ms，常态 SN 广播周期保持 500 ms。
- TX 继续采用 RTC 唤醒、CPU `WFE`、摄像头休眠、SPI 外设按需启停和 Radio 窗口接收等低功耗机制。
- 天线选路采用 RX 请求、TX 暂停图片并密集广播 SN、RX 扫描 12 路并保存前三候选的协商方案。

## 文档索引

1. [通信协议](01-通信协议.md)
2. [系统与目录结构](02-系统与目录结构.md)
3. [TX 软件框架](03-TX软件框架.md)
4. [RX 软件框架](04-RX软件框架.md)
5. [TX-RX 天线信号强度配置](05-TX-RX天线信号强度配置.md)
6. [TYGD31 旧工程 TX/RX 框架与天线扫描](06-TYGD31旧工程框架与天线扫描.md)
7. [新旧工程对比](07-新旧工程对比.md)
8. [STM 软件框架](08-STM软件框架.md)
9. [PC → RX 手动天线测试协议](09-PC到RX天线测试协议.md)
10. [手持设备控制器](10-手持设备控制器.md)（含模块框图与优化建议）

## 工程入口

| 模块 | 工程入口 | 作用 |
| --- | --- | --- |
| TX | `nrf_tx/examples/peripheral/radio/TX` | 拍照、JPEG读取、加速度采样、图片无线发送、低功耗及扫描协商 |
| TX无补光测试版 | `nrf_tx_led_off/examples/peripheral/radio/TX` | 与TX共用采集和无线流程，只关闭P0.08补光灯；Keil工程名为`nrf_tx_led_off` |
| RX | `nrf_rx/examples/peripheral/radio/receiver` | SN绑定、图片接收重组、12路天线管理、设备信息生成及UART转发 |
| STM | `stm32F103RET6/stm_project` | RX数据接收、RTC/设备信息补全、图片存储及PC通信 |
| 手持控制器 | `stm8sf2p6_project/USER/stm8sf2p6.ewp` | STM8按键检测、HC-04S配置、蓝牙保存照片命令发送 |
| TYGD31参考 | `TYGD31-2.4GHZ` | 保存旧TX/RX/STM实现，用于协议和天线算法追溯 |
| 上位机 | `tools` | 串口图片显示、YS文件查看、最近10包DeviceInfo和变化分析 |

## 维护原则

- TX/RX 的频率、Radio模式、包长和协议命令必须配套修改。
- `IMAGE_PERIOD_MS` 保持 500 ms，除非硬件和功耗测试明确要求调整。
- 修改天线扫描或PC手动测试时，应同步更新`01`、`04`、`05`、`07`、`09`和相关协议常量。
- TX常态SN广播实际与图片共用`IMAGE_PERIOD_MS`的RTC2事件；`SN_BROADCAST_PERIOD_MS`目前只用于启动日志，单独修改不会改变广播周期。
- 修改源代码后，先以当前`config.h`、`legacy_protocol.h`和实现函数为准核对本文档中的参数、方向和状态切换，不把文档中的测试建议写成已验证结论。
- RSSI 字节存放负 dBm 的绝对值，例如 `47` 表示约 `-47 dBm`，数值越小表示信号越强。
