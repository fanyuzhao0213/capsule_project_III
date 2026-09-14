/**
 * @file spi_bus.c
 * @brief 使用nRF52832 SPIM0和EasyDMA访问CX93510主机SPI口。
 */
#include "spi_bus.h"
#include "config.h"

#include <string.h>
#include "nrf.h"
#include "nrf_delay.h"
#include "nrf_gpio.h"
#include "nrf_log.h"
#if !(TX_LOG_ENABLED && TX_INIT_LOG_ENABLED)
#undef NRF_LOG_INFO
#undef NRF_LOG_WARNING
#undef NRF_LOG_ERROR
#define NRF_LOG_INFO(...)
#define NRF_LOG_WARNING(...)
#define NRF_LOG_ERROR(...)
#endif

/* CX93510主机SPI连接，数值来自已在实板验证通过的原理图网络。 */
#define CX_SPI_SCK_PIN   12u
#define CX_SPI_MOSI_PIN  14u
#define CX_SPI_MISO_PIN  15u
#define CX_SPI_CS_PIN    16u

/* EasyDMA只能访问RAM；使用独立且4字节对齐的收发缓冲。 */
static uint8_t m_tx_dma[SPI_BUS_MAX_TRANSFER] __ALIGNED(4);
static uint8_t m_rx_dma[SPI_BUS_MAX_TRANSFER] __ALIGNED(4);

/**
 * @brief 初始化SPIM0。
 *
 * 初始化顺序：先把CS拉高和SCK拉低，满足CX93510自动识别SPI的初始条件；
 * 再配置引脚、8 MHz、MSB优先、CPOL=0/CPHA=0，最后使能SPIM0。
 */
void spi_bus_init(void)
{
    nrf_gpio_cfg_output(CX_SPI_CS_PIN);
    nrf_gpio_pin_set(CX_SPI_CS_PIN);

    nrf_gpio_cfg_output(CX_SPI_SCK_PIN);
    nrf_gpio_pin_clear(CX_SPI_SCK_PIN); /* CX93510 supports CPOL=0, CPHA=0. */

    NRF_SPIM0->ENABLE = SPIM_ENABLE_ENABLE_Disabled;
    NRF_SPIM0->PSEL.SCK  = CX_SPI_SCK_PIN;
    NRF_SPIM0->PSEL.MOSI = CX_SPI_MOSI_PIN;
    NRF_SPIM0->PSEL.MISO = CX_SPI_MISO_PIN;
    NRF_SPIM0->FREQUENCY = SPIM_FREQUENCY_FREQUENCY_M8;
    NRF_SPIM0->CONFIG = (SPIM_CONFIG_ORDER_MsbFirst << SPIM_CONFIG_ORDER_Pos) |
                        (SPIM_CONFIG_CPOL_ActiveHigh << SPIM_CONFIG_CPOL_Pos) |
                        (SPIM_CONFIG_CPHA_Leading << SPIM_CONFIG_CPHA_Pos);
    NRF_SPIM0->ORC = 0x00u;
    NRF_SPIM0->ENABLE = SPIM_ENABLE_ENABLE_Enabled;
    NRF_LOG_INFO("CX SPI ready: SCK=P0.%02u MOSI=P0.%02u MISO=P0.%02u CS=P0.%02u, 8 MHz mode 0",
                 (unsigned)CX_SPI_SCK_PIN, (unsigned)CX_SPI_MOSI_PIN,
                 (unsigned)CX_SPI_MISO_PIN, (unsigned)CX_SPI_CS_PIN);
}

/**
 * @brief 执行一次SPI事务。
 *
 * 先把调用者数据复制到RAM中的EasyDMA缓冲，再拉低CS并启动SPIM0；
 * 等待END后拉高CS。最后延时1 us，满足CX93510要求的事务间隔大于74 ns。
 */
bool spi_bus_transfer(const uint8_t *tx, uint8_t *rx, size_t length)
{
    if ((length == 0u) || (length > SPI_BUS_MAX_TRANSFER))
    {
        return false;
    }

    if (tx != NULL)
    {
        memcpy(m_tx_dma, tx, length);
    }
    else
    {
        memset(m_tx_dma, 0, length);
    }

    NRF_SPIM0->TXD.PTR = (uint32_t)m_tx_dma;
    NRF_SPIM0->TXD.MAXCNT = length;
    NRF_SPIM0->RXD.PTR = (uint32_t)m_rx_dma;
    NRF_SPIM0->RXD.MAXCNT = length;
    NRF_SPIM0->EVENTS_END = 0u;

    nrf_gpio_pin_clear(CX_SPI_CS_PIN);
    NRF_SPIM0->TASKS_START = 1u;
    while (NRF_SPIM0->EVENTS_END == 0u) {}
    nrf_gpio_pin_set(CX_SPI_CS_PIN);

    /* The CX93510 requires HSS high for more than one 13.5 MHz host clock. */
    nrf_delay_us(1u);

    if (rx != NULL)
    {
        memcpy(rx, m_rx_dma, length);
    }
    return true;
}
