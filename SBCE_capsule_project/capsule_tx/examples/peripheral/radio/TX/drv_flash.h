/**
 * @file drv_flash.h
 * @brief 内部 Flash 驱动：擦页 + 写入 buffer
 *
 * 仿 TYGD31 旧工程 drv_flash 接口，内部基于 Nordic SDK 的 nrf_nvmc 实现。
 * 适用于本工程这种无 SoftDevice 的简单应用。
 */

#ifndef TX_DRV_FLASH_H
#define TX_DRV_FLASH_H

#include <stdint.h>



/**
 * @brief 擦除 1 个 Flash 页（nRF52832 一页 = 4096 字节）。
 * @param[in] addr 待擦除页内任意地址（驱动内部会按页对齐）。
 * @note  阻塞函数，擦除约需数 ms，期间 CPU 不能做其他事。
 */
void flash_page_erase(uint32_t addr);

/**
 * @brief 把 buffer 写入 Flash。
 * @param[in] addr 写入起始地址（必须 4 字节对齐）。
 * @param[in] buf  待写入数据指针。
 * @param[in] len  写入字节数。
 * @note  阻塞函数，写入期间 CPU 不能做其他事。
 *        地址与长度都必须 4 字节对齐，否则 NVMC 硬件会拒绝写入。
 */
void flash_buff_write(uint32_t addr, const void *buf, uint32_t len);



#endif /* TX_DRV_FLASH_H */

