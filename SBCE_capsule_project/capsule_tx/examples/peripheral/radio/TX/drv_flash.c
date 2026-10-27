/**
 * @file drv_flash.c
 * @brief 内部 Flash 驱动实现（基于 Nordic SDK nrf_nvmc）
 */

#include "drv_flash.h"
#include "nrf_nvmc.h"



/** @brief 擦除 1 个 Flash 页。 */
void flash_page_erase(uint32_t addr)
{
    /* nrf_nvmc_page_erase 内部按页对齐地址并等待擦除完成。 */
    nrf_nvmc_page_erase(addr);
}

/** @brief 把 buffer 写入 Flash。 */
void flash_buff_write(uint32_t addr, const void *buf, uint32_t len)
{
    /* nrf_nvmc_write_bytes 自动处理 4 字节对齐写入并等待完成。 */
    nrf_nvmc_write_bytes(addr, buf, len);
}

