# nrf_tx_20k_buffer

这是从 `nrf_tx` 独立复制的 TX 实验工程，原工程不修改。

Keil 工程：`examples/peripheral/radio/TX/pca10040/blank/arm5_no_packs/nrf_tx_20k_buffer.uvprojx`

## 与原 TX 的差异

1. 保留原有采集、BEGIN/DATA/END 包、ACK 和一次重发协议。
2. 采集完成后，从 CX93510 帧 RAM 分段读取完整 JPEG，放入 nRF 片内的 `m_image_buffer[20000]`。这里的 20 K 指旧工程使用的 **20,000 字节**，不是 20 KiB。现有协议仍限制实际 JPEG 不超过 19,252 字节。
3. 板级接线中 CX93510 的 `PD` 与 `PD_RAM` **都接到 P0.11**。只有整图读取成功才关闭 SPIM0 并拉低 P0.11，使芯片进入 Sleep；之后每个 DATA 分片和重发都从本地缓存取数。读取失败时丢弃本帧，不发送不完整图片。
4. 下一次采集前拉高 P0.11，重新执行 CX93510 接口检测和采集寄存器初始化。上电初始化结束、采集失败等路径也会使芯片进入 Sleep。

按这块板的实际接线，P0.11 低电平对应 `PD=0、PD_RAM=0`，即芯片数据手册定义的整芯片 Sleep 模式，而不是仅关闭帧 RAM。两根线的实际电平、功耗变化和重新唤醒后的采图结果仍需实板测量验证。

## 数据流

`OV7676 → CX93510 JPEG/帧RAM → 分段SPI读取 → nRF 20,000 B 整图缓存 → 242 B/包 → Radio`

缓存加载在 `image.c:image_cache_load()`；整芯片 Sleep/唤醒控制在 `cx93510.c:cx93510_power_down/up()`。修改后的工程应单独编译、烧录，重点验证连续拍照、丢包重发以及 P0.11 的波形。

## 编译与RAM占用

2026-09-29 使用 Keil ARM Compiler 5 编译通过：0 error、0 warning。`_build/nrf_tx_20k_buffer.map` 中 `m_image_buffer` 为 20,000 B，`Total RW Size` 为 32,552 B（含 8,192 B 栈预留）。基准 `nrf_tx.map` 为 12,552 B，新增量正好是 20,000 B。此结果仅验证编译和静态占用；帧 RAM 实际掉电、重新唤醒和连续图像正确性仍需上板测试。
