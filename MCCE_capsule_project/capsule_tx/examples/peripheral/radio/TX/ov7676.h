#ifndef OV7676_H
#define OV7676_H

/**
 * @file ov7676.h
 * @brief OV7676上电、身份检查及旧版480x480 YUV422 DVP初始化接口。
 */

#include <stdbool.h>
#include <stdint.h>

/**
 * @brief 初始化OV7676并启动连续输出。
 * @param chip_id 返回16位芯片ID，正常为0x7676；允许传入NULL。
 * @param revision 返回芯片修订号；允许传入NULL。
 */
bool ov7676_init(uint16_t *chip_id, uint8_t *revision);

/** @brief 将OV7676切换到软件休眠模式，保留已写入的寄存器配置。 */
bool ov7676_sleep(void);

/** @brief 从软件休眠唤醒OV7676，恢复连续视频输出。 */
bool ov7676_wakeup(void);

#endif
