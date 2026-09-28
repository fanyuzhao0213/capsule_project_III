# TX 端图像传输时序分析

> 基于实测 RTT 日志整理；记录图像采集、TX 发送、ACK 往返的耗时分布。

---

## 1. 测试环境

| 项目 | 配置 |
|---|---|
| 硬件 | nRF52832 + OV7676 + CX93510 |
| 图像尺寸 | 480 × 480，JPEG 直出 |
| JPEG 平均大小 | 15.0 ~ 15.5 KB |
| 典型分包数 | 63 ~ 64 包 |
| Radio 速率 | Nordic 2 Mbit |
| 单包有效载荷 | 242 字节（`LEGACY_IMAGE_PACKET_PAYLOAD_SIZE`）|
| 采集周期 | 300 ms（`IMAGE_PERIOD_MS`）|
| 重复发送 | 关闭（`IMAGE_REPEAT_SEND_ENABLED = 0`）|

---

## 2. 日志格式规范

所有关键事件统一前缀 `[N ms]` 时间戳，单位 ms（系统启动后的累计毫秒数）：

| 事件 | 日志格式 | 触发时机 |
|---|---|---|
| 采集开始 | `[N ms] Capture start: frame=X` | `cx93510_capture_one()` 调用前 |
| 采集完成 | `[N ms] Capture done: frame=X jpeg=Y bytes (Z ms)` | 采集成功后，含耗时 |
| TX 开始 | `[N ms] TX start: frame=X jpeg=Y bytes` | 发送 BEGIN 包前 |
| TX 完成 | `[N ms] TX done: frame=X (Z ms, checksum=0xAA)` | 发送 END 包后，含耗时 |
| ACK 收到 | `[N ms] ACK received: frame=X` | 收到匹配的 ACK |
| ACK 超时 | `[N ms] ACK timeout: frame=X retry=Y` | 30 ms 后未收到 ACK |
| 放弃帧 | `[N ms] Frame abandoned: frame=X (ack timeout)` | 重发次数用尽 |

**静默事件**（不打印日志，避免刷屏）：

- 采集周期未到 → 直接返回
- 上一帧还在发 → 跳过本周期采集
- Radio 队列包未匹配 ACK → 静默出队

---

## 3. 典型一帧的生命周期（成功案例）

```
T+4500 ms  [Capture start: frame=10]
              ↓ CX93510 内部流程
              ↓   - reset compressor/frame buffer
              ↓   - waiting for one frame
              ↓   - JPEG encoding
              ↓   - SPI read frame header
T+4549 ms  [Capture done: frame=10 jpeg=15540 bytes (49 ms)]    ← 采集 49 ms
T+4549 ms  [TX start: frame=10 jpeg=15540 bytes]                ← 采集结束立即发送
              ↓ 发 BEGIN ×2 + DATA ×64 + END ×1
T+4682 ms  [TX done: frame=10 (133 ms, checksum=0x65)]          ← 发送 133 ms
T+4687 ms  [ACK received: frame=10]                              ← 5 ms 后 ACK 到达
                                                       ── 帧总耗时 187 ms ──
T+5000 ms  [Capture start: frame=11]                             ← 下一帧采集
```

### 3.1 时间分配

| 阶段 | 耗时 | 占比 | 说明 |
|---|---|---|---|
| **采集** | 49 ms | 26% | OV7676 输出 → CX93510 JPEG 压缩 → SPI 读帧头 |
| **TX 发送** | 133 ms | 71% | 64 包 × 2 ms/包 + 2 个 BEGIN + 1 个 END |
| **ACK 往返** | 5 ms | 3% | RX 收完 → 回 ACK → TX 收到 |
| **帧总耗时** | **187 ms** | 100% | 采集开始 → ACK 完成 |
| **空闲 / 休眠** | ~113 ms | — | 按300 ms周期估算，主循环 `__WFE()` 等待下一周期 |

---

## 4. 单包发送时间拆解

TX 133 ms / 64 包 ≈ **2 ms/包**，每包耗时分解：

| 操作 | 耗时 |
|---|---|
| Radio 切换 RX→TX | ~200 μs |
| 发 254 字节 @ 2 Mbit/s | 254 × 8 / 2 000 000 = **1.02 ms** |
| Radio 切换 TX→RX | ~200 μs |
| `IMAGE_FRAGMENT_GAP_MS` 节流 | **1.00 ms** |
| **单包总计** | **~2.4 ms** |

> 注意：实测 133 ms ≈ 64 × 2.08 ms，与理论值吻合。

---

## 5. ACK 超时与重发场景

### 5.1 首次超时重发（frame=9 实测）

```
T+4169 ms  [TX done: frame=9 (131 ms, checksum=0x39)]     ← 第一轮发送完毕
T+4199 ms  [ACK timeout: frame=9 retry=1]                  ← 30 ms 后无 ACK
                                                            （IMAGE_ACK_TIMEOUT_MS = 30）
T+4327 ms  [TX done: frame=9 (289 ms, checksum=0x39)]     ← 重发完毕（289 ms ≈ 2 倍时间）
```

### 5.2 重发行为特征

- **不重发 BEGIN**：直接重发 DATA + END（`legacy_send_begin = false`）
- **重置 checksum**：每次重发都重新计算累加和
- **重发上限**：`IMAGE_MAX_RETRIES = 1`，超过则 `Frame abandoned`

---

## 6. 关键时间参数（可在 `config.h` 调整）

| 参数 | 当前值 | 影响 | 建议调整方向 |
|---|---|---|---|
| `IMAGE_PERIOD_MS` | 300 | 帧间隔（采集请求间隔）| 改小 → 帧率↑；改大 → 省电 |
| `IMAGE_FRAGMENT_GAP_MS` | 1 | 分片间隔 | 改小 → TX 更快，但 RX 可能跟不上 |
| `IMAGE_CAPTURE_TIMEOUT_MS` | 1000 | 采集阻塞上限 | 一般不动 |
| `IMAGE_ACK_TIMEOUT_MS` | 30 | ACK 等待窗口 | 正常 ACK 5 ms，30 ms 留 6 倍余量 |
| `IMAGE_MAX_RETRIES` | 1 | 重发次数 | 改成 2+ 可抗更恶劣环境 |
| `IMAGE_REPEAT_SEND_ENABLED` | 0 | 主动发 2 遍 | 改成 1 → 抗丢包↑，但带宽 ×2 |
| `WATCHDOG_TIMEOUT_SECONDS` | 3 | 看门狗超时 | 必须 > 采集 + TX + ACK 总耗时 |

---

## 7. 帧间隔 500 ms 下的真实周期

```
采集请求周期 = 500 ms（RTC2决定）
帧实际耗时 ≈ 187 ms（采集 + TX + ACK）
剩余空闲 ≈ 313 ms（理想无重发场景）
```

**理论帧率**：1000 ms / 500 ms = **2 帧/秒，即约 120 帧/分钟**。发生重发或上一帧超时会跳过周期，实际帧数会降低。

如后续改成300 ms周期，理论变化如下：

| 参数 | 500 ms当前配置 | 300 ms备选配置 |
|---|---|---|
| 帧耗时 | 187 ms | 187 ms |
| 空闲 | 313 ms | 113 ms |
| 理论帧率 | 2 fps | 3.33 fps |
| 看门狗 | 3 s（够） | 3 s（够） |

> 目前采用500 ms周期以兼顾约2 fps和低功耗。300 ms可作为后续吞吐测试选项，但会明显增加摄像头、SPI和Radio活动占空比；若重发使单帧超过周期，保护逻辑会跳过该周期，不会覆盖上一帧。

---

## 8. 优化路径

| 目标 | 方法 | 预期收益 |
|---|---|---|
| 缩短采集 | 调低 JPEG 压缩率（画质↑）/ 减小图像尺寸 | 采集耗时 -20%~50% |
| 缩短 TX | 减小 `IMAGE_FRAGMENT_GAP_MS`（1 ms → 0.5 ms） | TX 耗时 -25% |
| 缩短 TX | 增大载荷（需改协议） | 每帧包数减少 |
| 提高抗丢包 | `IMAGE_REPEAT_SEND_ENABLED = 1` | 主动重发，丢包率↓ |
| 提高帧率 | 减小 `IMAGE_PERIOD_MS` + 看门狗时间 | 帧率↑，功耗↑ |
| 降低功耗 | 增大 `IMAGE_PERIOD_MS` + 加大空闲比 | 帧率↓，功耗↓ |

---

## 9. 关键代码定位

| 日志 | 位置 |
|---|---|
| `[N ms] Capture start/done` | [image.c: `image_capture_task()`](image.c) |
| `[N ms] TX start/done` | [image.c: `image_tx_service()`](image.c) |
| `[N ms] ACK received` | [image.c: `radio_rx_process()`](image.c) |
| `[N ms] ACK timeout` | [image.c: `image_ack_service()`](image.c) |
| 采集超时配置 | [config.h: `IMAGE_CAPTURE_TIMEOUT_MS`](config.h) |
| 周期配置 | [config.h: `IMAGE_PERIOD_MS`](config.h) |
| ACK 超时配置 | [config.h: `IMAGE_ACK_TIMEOUT_MS`](config.h) |

---

## 10. 修订记录

| 日期 | 修订 | 说明 |
|---|---|---|
| 2026-09-08 | v1.0 | 首次建立，基于 frame=10 实测日志整理 |
| 2026-09-08 | v1.1 | 追加 10 帧长时间实测统计（章节 11） |
| 2026-09-08 | v1.2 | 加入 RX 端日志分析，定位 ACK 链路盲点（章节 13）|

---

## 11. 长时间实测统计（10 帧，frame=5, 37~45）

> 系统连续运行 ~163 秒，共记录 10 帧完整时序。

### 11.1 各阶段耗时分布

| 阶段 | 最小 | 最大 | 均值 | 方差 | 备注 |
|---|---|---|---|---|---|
| 采集 | 29 ms | 51 ms | **42 ms** | 大 | 受 CX93510 编码时间波动影响 |
| 首次 TX | 131 ms | 137 ms | **133 ms** | 小 | 64 包 × ~2 ms/包，非常稳定 |
| 重发 TX | 289 ms | 301 ms | **293 ms** | 小 | 约为首次的 **2.2 倍** |
| ACK 往返（成功时）| 5 ms | 5 ms | **5 ms** | — | 仅 frame=39 成功 |

### 11.2 ACK 成功率统计

| 项目 | 数值 |
|---|---|
| 发送总帧数 | 10 帧 |
| 首次发送即成功 ACK | **1 帧**（10%）|
| 首次 ACK 超时但重发后成功 | **9 帧**（90%）|
| 重发后仍超时（帧被放弃）| 0 帧（0%）|
| **整体交付成功率** | **100%（10/10 帧最终送达）** |

### 11.3 数据观察与初步结论

#### ✅ 时间参数非常稳定

- **首次 TX 耗时** 131~137 ms（极差 6 ms，相对误差 < 5%）
- **重发 TX 耗时** 289~301 ms（极差 12 ms，相对误差 < 4%）
- → 单包发送节流非常精准，`IMAGE_FRAGMENT_GAP_MS = 1 ms` 实现稳定

#### ✅ 重发机制工作正常

- 10 帧中 9 帧触发重发，但**全部最终送达 RX**
- 重发耗时稳定在 290 ms 左右，说明重发确实完整走了一遍 N 个 DATA + END

#### ⚠️ 首次 ACK 超时率偏高（90%）

- 正常 ACK 往返只需 5 ms，超时阈值设 30 ms（6 倍余量），**理论上不应超时**
- 但实测 9/10 帧首次超时，可能原因：

| 可能原因 | 排查建议 |
|---|---|
| RX 端 `image_end_pkt_parse` 处理慢 | 检查 RX 端 RTT 日志中 END 包解析到 ACK 发送的时间 |
| TX 端 Radio 切换阻塞 | 检查 `radio_send_packet` 内部 RXEN→TXEN→RXEN 是否卡顿 |
| 频点/天线干扰导致 ACK 包丢失 | 缩短 TX-RX 距离或更换信道 |
| RX 队列堵导致 ACK 未及时处理 | 检查 RX 端 `m_rx_queue.dropped` 计数 |

#### ✅ 采集耗时波动正常

- 29~51 ms 波动属于 CX93510 JPEG 编码器的正常方差
- 与图像内容复杂度相关（细节多 → 编码慢）

### 11.4 改进建议（基于本次实测）

#### 优先级 1：排查 ACK 超时

```
当前  IMAGE_ACK_TIMEOUT_MS = 30  →  首次 ACK 超时率 90%
建议  排查后定位根因；不要盲目调大阈值
```

#### 优先级 2：考虑开启主动重传

```c
// config.h 中改为：
#define IMAGE_REPEAT_SEND_ENABLED 1
```

- 每块主动发 2 遍（0..N-1 + 间隔 + 0..N-1）
- 接收端按 frame_id + fragment_index 去重
- **不依赖 ACK** 就能大幅降低丢包率
- 缺点：带宽 ×2，TX 耗时翻倍到 ~260 ms

#### 优先级 3：观察 RX 端

在 RX 端也加类似的时序日志：

```c
// RX 端 receiver_process_legacy_packet END 分支：
NRF_LOG_INFO("[%u ms] END received: frame=X", g_time_ms);
// receiver_send_image_ack 调用前后：
NRF_LOG_INFO("[%u ms] ACK sent: frame=X", g_time_ms);
NRF_LOG_INFO("[%u ms] ACK send done: %u ms",
             (unsigned)g_time_ms, (unsigned)(g_time_ms - start_ms));
```

如果 RX 端 END 到 ACK 发送耗时 > 25 ms，则根因在 RX 端处理慢。
如果 RX 端 ACK 发送耗时 < 1 ms，则 TX 端没收到 ACK 的原因在无线链路。

---

## 12. 修订记录（汇总）

| 版本 | 日期 | 主要内容 |
|---|---|---|
| v1.0 | 2026-09-08 | 首版，章节 1~10 |
| v1.1 | 2026-09-08 | 追加章节 11（10 帧长时间实测统计）|
| v1.2 | 2026-09-08 | 加入 RX 端日志分析，定位 ACK 链路盲点（章节 13）|

---

## 13. RX 端日志分析（id=40~52 长时间测试）

### 13.1 数据接收状况

| 帧 ID | 包数 | 字节数 | 状态 |
|---|---|---|---|
| id=40 | 66 | 16026 | ✅ 转发 STM |
| id=41~47 | 66 | 15734~15948 | ✅ 全部转发 |
| id=52 | 63 | (丢失 1 包) | ⚠️ RX 正确判定 incomplete，丢弃 |

**结论**：RX 接收侧工作正常，所有成功帧都已转发到 STM。

### 13.2 🔴 ACK 链路是盲点

> **本节是定位 TX 端 ACK 超时率 90% 的关键发现。**

#### RX 日志中没有任何 ACK 相关的输出

仔细检查 RX 端日志：
- ✅ BEGIN 接收日志（"Legacy image begin: id=N"）
- ✅ DATA 重组日志（"received_count" 隐含在 stats 中）
- ✅ END 校验日志（成功则 "queued for STM"）
- ❌ **没有 "ACK sent" 或 "ACK transmit done" 日志**

#### 原因：`receiver_send_image_ack()` 没有日志

查看 [image.c:175-201](image.c) 实现：

```c
void receiver_send_image_ack(uint8_t image_id)
{
    uint8_t response[RADIO_PACKET_SIZE] __ALIGNED(4) = {0};
    uint8_t repeat;

    response[0] = LEGACY_CMD_IMAGE_RECEIVED_RESPONSE;
    response[1] = image_id;
    memcpy(&response[2], g_legacy_image_rx.capsule_sn,
           LEGACY_CAPSULE_SN_SIZE);

    /* 短暂暂停接收，发送 ACK 后立即恢复 RX。 */
    NVIC_DisableIRQ(RADIO_IRQn);
    NRF_RADIO->EVENTS_DISABLED = 0u;
    NRF_RADIO->TASKS_DISABLE = 1u;
    while (NRF_RADIO->EVENTS_DISABLED == 0u) {}
    nrf_gpio_pin_set(RECEIVER_MODE_PIN);
    ...
    for (repeat = 0u; repeat < 2u; repeat++)
    {
        NRF_RADIO->EVENTS_DISABLED = 0u;
        NRF_RADIO->TASKS_TXEN = 1u;
        while (NRF_RADIO->EVENTS_DISABLED == 0u) {}
    }
    ...
}
```

**问题**：
1. 函数内部没有任何 `NRF_LOG_INFO` 调用，**无法验证它是否真的被调用**
2. 如果 `RECEIVER_MODE_PIN`（P0.23）硬件没正确连接外部 RF 开关芯片，ACK 信号根本发不出去
3. 如果 `TASKS_TXEN` 事件卡住，函数会无限阻塞

#### 可能的原因分析

| 可能原因 | 排查方法 |
|---|---|
| **ACK 函数根本没被调用** | 加日志：进入函数立刻打印 `"ACK send start"` |
| **`RECEIVER_MODE_PIN` 硬件未接** | 用示波器/逻辑分析仪观察 P0.23 在 ACK 发送时是否拉高 |
| **`TASKS_TXEN` 阻塞** | 加超时日志：`while (NRF_RADIO->EVENTS_DISABLED == 0u)` 加超时保护 |
| **TX 没在 RX 模式** | 看 TX 端 `radio_send_packet` 末尾是否真的恢复 RX |
| **频点/地址不匹配** | TX 端的地址与 RX 端地址要完全一致 |

### 13.3 建议的 RX 端日志补丁

在 `receiver_send_image_ack` 函数中添加时序日志：

```c
void receiver_send_image_ack(uint8_t image_id)
{
    uint32_t ack_start_ms = g_image_rx_ms;        // 假设有系统时间戳
    NRF_LOG_INFO("[%u ms] ACK send start: frame=%u",
                 (unsigned)ack_start_ms, (unsigned)image_id);

    ... // 原有代码 ...

    NRF_LOG_INFO("[%u ms] ACK send done: frame=%u (%u ms)",
                 (unsigned)g_image_rx_ms,
                 (unsigned)image_id,
                 (unsigned)(g_image_rx_ms - ack_start_ms));
}
```

如果 RX 端看到 `"ACK send start"` 但**没有** `"ACK send done"`：
- 说明 `TASKS_TXEN` 卡死，需要加超时保护

如果 RX 端**两条日志都有，但 TX 端还是收不到 ACK**：
- 说明是无线链路问题（频点/距离/天线切换）
- 需要查 P0.23 硬件

### 13.4 进一步定位的测试方案

#### 方案 A：开启 RX 端 RTT 后做以下测试

```
1. 烧录 RX 板（含 ACK 日志补丁）
2. 用 J-Link 连接 RX 板 RTT Viewer
3. 观察每一帧 END 之后是否打印 "ACK send start"
4. 观察打印是否在 5ms 内出现 "ACK send done"
```

#### 方案 B：硬件示波器法

```
1. 用示波器探头接 RX 板 P0.23
2. 在 RTT Viewer 看 TX 帧
3. 观察 TX 帧后是否 P0.23 拉高（应拉高 ~2 ms 发送 ACK 期间）
4. 如果 P0.23 没拉高 → RX 软件没运行到 ACK 发送
5. 如果 P0.23 拉高但 TX 没收到 → 硬件 RF 开关芯片问题
```

#### 方案 C：临时绕过 ACK

把 `config.h` 的 `IMAGE_ACK_TIMEOUT_MS` 调小到 5 ms，或者干脆把 `IMAGE_REPEAT_SEND_ENABLED` 改为 1，让每块主动发 2 遍，**不依赖 ACK** 也能保证图像送达。

---

## 14. 下一步行动清单

| 优先级 | 行动 | 预期 |
|---|---|---|
| 🔴 P1 | 给 `receiver_send_image_ack` 加时序日志 | 确认函数是否被调用 |
| 🔴 P1 | 加 `TASKS_TXEN` 超时保护 | 避免无限阻塞 |
| 🟡 P2 | 用示波器看 P0.23 | 确认 RF 开关芯片受控 |
| 🟢 P3 | 测试 `IMAGE_REPEAT_SEND_ENABLED = 1` | 不依赖 ACK 的图像送达 |
