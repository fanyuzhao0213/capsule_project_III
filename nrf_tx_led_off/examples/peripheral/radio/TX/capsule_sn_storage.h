/**
 * @file capsule_sn_storage.h
 * @brief 胶囊序列号存储接口：出厂 ID 与用户设定 SN 的统一管理
 *
 * 设计要点：
 * - 启动时自动从 Flash 读取设定 SN；若 Flash 未写入（默认 0xFF）则使用 FICR DEVICEID；
 * - 对外暴露 `capsule_sn_storage_get_active()` 单一指针，调用方无需关心当前是出厂还是设定；
 * - 写入接口（capsule_sn_storage_write）预留，依赖平台 Flash 驱动；
 * - 设定检测（capsule_sn_storage_set_checking）保留 TYGD31 风格的命令监听与应答逻辑。
 */

#ifndef TX_CAPSULE_SN_STORAGE_H
#define TX_CAPSULE_SN_STORAGE_H

#include <stdbool.h>
#include <stdint.h>
#include "legacy_protocol.h"



/**
 * @brief 8 字节胶囊序列号结构。
 */
typedef struct
{
    uint8_t bytes[LEGACY_CAPSULE_SN_SIZE];
} capsule_sn_t;



/**
 * @brief 初始化 SN 存储：从 Flash 读取设定 SN，决定 active 指针指向。
 * @note  必须在 main 启动早期调用，image_tx_service 等模块会通过 get_active 获取当前 SN。
 */
void capsule_sn_storage_init(void);

/**
 * @brief 获取当前生效的 8 字节 SN 指针。
 * @return 指向 8 字节序列号的常量指针；可能是 FICR DEVICEID 或 Flash 中的设定值。
 * @note  返回的指针生命周期与整个程序一致，调用方不应修改其内容。
 */
const uint8_t *capsule_sn_storage_get_active(void);

/**
 * @brief 将新的 8 字节 SN 写入 Flash 并切换 active 指针。
 * @param[in] sn 待写入的序列号指针。
 * @return true 写入成功；false 写入失败（Flash 驱动未实现或参数错误）。
 * @note  本接口为预留能力，依赖平台 Flash 驱动（drv_flash）。
 *        当前实现返回 false，待后续集成 drv_flash 后启用。
 */
bool capsule_sn_storage_write(const capsule_sn_t *sn);

#endif /* TX_CAPSULE_SN_STORAGE_H */
