/**
 * @file rf1662.h
 * @brief RF1662SR 12 路射频天线开关驱动。
 *
 * 控制时序与 TYGD31-2.4GHZ/TYGD_RX/device/dev_rf1662.c 保持一致。
 */

#ifndef RX_RF1662_H
#define RX_RF1662_H

#include <stdbool.h>
#include <stdint.h>

/** RF1662 实际连接的天线数量。 */
#define RF1662_ANTENNA_COUNT 12u

/**
 * @brief 初始化 RF1662，并选择指定天线。
 * @param antenna_index 天线下标，0 表示 ANT1，11 表示 ANT12。
 * @return 参数有效时返回 true，否则返回 false。
 *
 * @note 应在 Radio 启动前调用。
 */
bool rf1662_init(uint8_t antenna_index);

/**
 * @brief 切换 RF1662 天线通道。
 * @param antenna_index 天线下标，0 表示 ANT1，11 表示 ANT12。
 * @return 参数有效时返回 true，否则返回 false。
 *
 * @note 切换期间射频通路短暂不可用，不要在接收 Radio 数据包时调用。
 */
bool rf1662_select_antenna(uint8_t antenna_index);

/** @brief 获取当前选择的天线下标。 */
uint8_t rf1662_get_antenna(void);

#endif /* RX_RF1662_H */
