#ifndef SPI_BUS_H
#define SPI_BUS_H

/**
 * @file spi_bus.h
 * @brief nRF52832 SPIM0访问CX93510的底层接口。
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* nRF52832 SPIM0单次EasyDMA传输最大按255字节使用。 */
#define SPI_BUS_MAX_TRANSFER 255u

/** @brief 初始化CX93510使用的8 MHz、Mode 0主机SPI。 */
void spi_bus_init(void);

/** @brief 重新使能已初始化的SPIM0，供CX93510活动阶段使用。 */
void spi_bus_resume(void);

/** @brief 关闭空闲SPIM0，降低CX93510掉电期间的外设功耗。 */
void spi_bus_suspend(void);

/**
 * @brief 在CS保持低电平期间完成一次全双工SPI传输。
 * @param tx 发送数据；为NULL时发送全0。
 * @param rx 接收缓冲；为NULL时丢弃接收结果。
 * @param length 传输字节数，范围1～255。
 */
bool spi_bus_transfer(const uint8_t *tx, uint8_t *rx, size_t length);

#endif
