/**
 * @file capsule_sn_storage.c
 * @brief 胶囊序列号存储实现：FICR 出厂 ID + 用户 Flash 设定切换
 */

#include "capsule_sn_storage.h"
#include "config.h"
#include "drv_flash.h"
#include "nrf.h"
#include "nrf_log.h"
#include <string.h>

#if !(TX_LOG_ENABLED && TX_CONFIG_LOG_ENABLED)
#undef NRF_LOG_INFO
#undef NRF_LOG_WARNING
#undef NRF_LOG_ERROR
#undef NRF_LOG_HEXDUMP_INFO
#define NRF_LOG_INFO(...)
#define NRF_LOG_WARNING(...)
#define NRF_LOG_ERROR(...)
#define NRF_LOG_HEXDUMP_INFO(...)
#endif



/* ============================================================
 * 平台相关常量
 * ============================================================ */

/** 芯片出厂 64 位 DEVICEID 的起始地址（nRF52832 FICR）。 */
#define FICR_DEVICE_ID_ADDR        (NRF_FICR_BASE + 0x060u)



/* ============================================================
 * 内部状态
 * ============================================================ */

/**
 * 当前生效的 SN 指针：
 * - 指向 FICR_DEVICE_ID_ADDR：未绑定，使用出厂 ID；
 * - 指向 FLASH_CAPSULE_SN_ADDR：已绑定，使用用户设定 SN。
 */
static const uint8_t *g_active_sn = (const uint8_t *)FICR_DEVICE_ID_ADDR;



/* ============================================================
 * 内部辅助
 * ============================================================ */

/**
 * @brief 判断指定 buffer 是否全为 0xFF（Flash 擦除后默认状态）。
 */
static bool buffer_is_all_ff(const uint8_t *buf, uint32_t len)
{
    uint32_t i;
    for (i = 0u; i < len; ++i)
    {
        if (buf[i] != 0xFFu)
        {
            return false;
        }
    }
    return true;
}



/* ============================================================
 * 对外接口
 * ============================================================ */

/** @brief 初始化：从 Flash 读取设定 SN，决定 active 指针指向。 */
void capsule_sn_storage_init(void)
{
    const uint8_t *flash_sn = (const uint8_t *)FLASH_CAPSULE_SN_ADDR;

    /* Flash 未写入（全 0xFF）→ 使用出厂 ID；否则 → 使用用户设定 SN。 */
    if (buffer_is_all_ff(flash_sn, LEGACY_CAPSULE_SN_SIZE))
    {
        g_active_sn = (const uint8_t *)FICR_DEVICE_ID_ADDR;
    }
    else
    {
        g_active_sn = flash_sn;
    }
}

/** @brief 获取当前生效 SN 指针。 */
const uint8_t *capsule_sn_storage_get_active(void)
{
    return g_active_sn;
}

/** @brief 写入新 SN 到 Flash（依赖 drv_flash 驱动）。 */
bool capsule_sn_storage_write(const capsule_sn_t *sn)
{
    if (sn == NULL)
    {
        NRF_LOG_ERROR("[SN FLASH] write rejected: null SN");
        return false;
    }

    NRF_LOG_INFO("[SN FLASH] erase page at 0x%08x",
                 (unsigned)FLASH_CAPSULE_SN_ADDR);
    NRF_LOG_HEXDUMP_INFO(sn->bytes, LEGACY_CAPSULE_SN_SIZE);

    /* 写入期间 CPU 会被 NVMC 阻塞数毫秒，期间无线收发会暂停；
     * 调用方需自行确保此时不会有关键的 Radio 操作正在等待。
     */
    flash_page_erase(FLASH_CAPSULE_SN_ADDR);
    flash_buff_write(FLASH_CAPSULE_SN_ADDR, sn->bytes, LEGACY_CAPSULE_SN_SIZE);

    if (memcmp((const void *)FLASH_CAPSULE_SN_ADDR,
               sn->bytes, LEGACY_CAPSULE_SN_SIZE) != 0)
    {
        NRF_LOG_ERROR("[SN FLASH] verify FAILED");
        return false;
    }

    /* 重新读取 Flash，让 active 指针切换到新写入的 SN。 */
    capsule_sn_storage_init();
    NRF_LOG_INFO("[SN FLASH] write and verify OK");
    return true;
}
