/**
 * @file antenna_manager.h
 * @brief 12路天线发现、协商扫描、候选排序和失联切换。
 */

#ifndef RX_ANTENNA_MANAGER_H
#define RX_ANTENNA_MANAGER_H

#include <stdbool.h>
#include <stdint.h>
#include "config.h"
#include "rf1662.h"

/** @brief 初始化为未绑定发现状态。 */
void receiver_antenna_init(void);

/** @brief TIMER1每1 ms调用，只累计调度时间。 */
void receiver_antenna_tick_1ms(uint32_t elapsed_ms);

/** @brief 处理END和0x11产生的紧急事件。 */
bool receiver_antenna_service_events(void);

/** @brief 按4 ms节拍推进当前天线状态。 */
bool receiver_antenna_service_schedule(void);

/**
 * @brief Radio中断提交一个刚接收的包。
 * @return true表示扫描模块已消费该包，不应进入业务队列。
 */
bool receiver_antenna_on_radio_packet(const uint8_t *packet,
                                      bool crc_ok, uint8_t rssi);

/** @brief 绑定或解绑后重置对应天线策略。 */
void receiver_antenna_binding_changed(void);

/** @brief 处理主循环收到的0x11快速扫描通知。 */
void receiver_antenna_fast_scan_granted(const uint8_t *sn);

/** @brief 收到完整图片后刷新链路健康状态。 */
void receiver_antenna_note_complete_image(void);

/** @brief 图片丢失或校验失败时增加失败计数。 */
void receiver_antenna_note_frame_failure(void);

/** @brief 未绑定发现SN后延长当前天线驻留时间。 */
void receiver_antenna_note_discovery_sn(void);

/** @brief 如果正在等待扫描，则使用本次END窗口发送0x12。 */
bool receiver_antenna_try_fast_scan_request(uint8_t image_id,
                                            const uint8_t *capsule_sn);

/** @brief 当前是否正在等待利用END窗口发起快速扫描。 */
bool receiver_antenna_scan_request_pending(void);

/** @brief 把最近一次完整扫描的12路RSSI复制给设备信息。 */
void receiver_antenna_copy_latest_rssi(
    uint8_t rssi[RF1662_ANTENNA_COUNT]);

#endif /* RX_ANTENNA_MANAGER_H */
