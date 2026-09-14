/**
 * @file dev_adxl362.c
 * @brief nRF52832通过SPIM1轮询读取ADXL362三轴加速度计。
 *
 * 板上INT1连接到P0.21/nRESET，因此本驱动明确关闭所有中断映射，
 * 不配置GPIO中断，也不改变nRF52832的复位脚配置。
 */

#include "dev_adxl362.h"
#include "config.h"
#include "nrf.h"
#include "nrf_delay.h"
#include "nrf_gpio.h"
#include "nrf_log.h"
#include <stddef.h>

#if !(TX_LOG_ENABLED && ADXL362_LOG_ENABLED)
#undef NRF_LOG_INFO
#undef NRF_LOG_WARNING
#undef NRF_LOG_ERROR
#define NRF_LOG_INFO(...)
#define NRF_LOG_WARNING(...)
#define NRF_LOG_ERROR(...)
#endif

/* 原理图网络：362SPI_MOSI/SCLK/CS/MISO。 */
#define ADXL362_MOSI_PIN       0u
#define ADXL362_SCK_PIN        1u
#define ADXL362_CS_PIN         3u
#define ADXL362_MISO_PIN       5u

#define ADXL362_CMD_WRITE      0x0Au
#define ADXL362_CMD_READ       0x0Bu
#define ADXL362_REG_DEVID_AD   0x00u
#define ADXL362_REG_PARTID     0x02u
#define ADXL362_REG_XDATA_L    0x0Eu
#define ADXL362_REG_SOFT_RESET 0x1Fu
#define ADXL362_REG_FILTER_CTL 0x2Cu
#define ADXL362_REG_POWER_CTL  0x2Du
#define ADXL362_REG_INTMAP1    0x2Au
#define ADXL362_REG_INTMAP2    0x2Bu

#define ADXL362_DEVID_AD_VALUE 0xADu
#define ADXL362_PARTID_VALUE   0xF2u
#define ADXL362_SOFT_RESET_KEY 0x52u
#define ADXL362_FILTER_2G_100HZ 0x13u
#define ADXL362_MEASURE_MODE   0x02u
#define ADXL362_STANDBY_MODE   0x00u
#define ADXL362_SPI_TIMEOUT    1000000u

static uint8_t m_tx[10] __ALIGNED(4);
static uint8_t m_rx[10] __ALIGNED(4);
static bool m_ready;
static bool m_measuring;

static bool adxl362_transfer(size_t length)
{
    uint32_t timeout = ADXL362_SPI_TIMEOUT;

    NRF_SPIM1->TXD.PTR = (uint32_t)m_tx;
    NRF_SPIM1->TXD.MAXCNT = length;
    NRF_SPIM1->RXD.PTR = (uint32_t)m_rx;
    NRF_SPIM1->RXD.MAXCNT = length;
    NRF_SPIM1->EVENTS_END = 0u;
    nrf_gpio_pin_clear(ADXL362_CS_PIN);
    NRF_SPIM1->TASKS_START = 1u;
    while ((NRF_SPIM1->EVENTS_END == 0u) && (timeout != 0u))
    {
        timeout--;
    }
    nrf_gpio_pin_set(ADXL362_CS_PIN);
    if (timeout == 0u)
    {
        NRF_SPIM1->TASKS_STOP = 1u;
        return false;
    }
    return true;
}

static bool adxl362_write_reg(uint8_t address, uint8_t value)
{
    m_tx[0] = ADXL362_CMD_WRITE;
    m_tx[1] = address;
    m_tx[2] = value;
    return adxl362_transfer(3u);
}

static bool adxl362_read_regs(uint8_t address, uint8_t *data, size_t length)
{
    size_t i;

    if ((data == NULL) || (length == 0u) || (length > 8u))
    {
        return false;
    }
    m_tx[0] = ADXL362_CMD_READ;
    m_tx[1] = address;
    for (i = 0u; i < length; i++)
    {
        m_tx[i + 2u] = 0u;
        m_rx[i + 2u] = 0u;
    }
    if (!adxl362_transfer(length + 2u))
    {
        return false;
    }
    for (i = 0u; i < length; i++)
    {
        data[i] = m_rx[i + 2u];
    }
    return true;
}

bool adxl362_init(void)
{
    uint8_t id[3];

    m_ready = false;
    m_measuring = false;
    nrf_gpio_cfg_output(ADXL362_CS_PIN);
    nrf_gpio_pin_set(ADXL362_CS_PIN);
    nrf_gpio_cfg_output(ADXL362_SCK_PIN);
    nrf_gpio_pin_clear(ADXL362_SCK_PIN);
    nrf_gpio_cfg_output(ADXL362_MOSI_PIN);
    nrf_gpio_cfg_input(ADXL362_MISO_PIN, NRF_GPIO_PIN_NOPULL);

    NRF_SPIM1->ENABLE = SPIM_ENABLE_ENABLE_Disabled;
    NRF_SPIM1->PSEL.SCK = ADXL362_SCK_PIN;
    NRF_SPIM1->PSEL.MOSI = ADXL362_MOSI_PIN;
    NRF_SPIM1->PSEL.MISO = ADXL362_MISO_PIN;
    NRF_SPIM1->FREQUENCY = SPIM_FREQUENCY_FREQUENCY_M2;
    NRF_SPIM1->CONFIG = (SPIM_CONFIG_ORDER_MsbFirst << SPIM_CONFIG_ORDER_Pos) |
                        (SPIM_CONFIG_CPOL_ActiveHigh << SPIM_CONFIG_CPOL_Pos) |
                        (SPIM_CONFIG_CPHA_Leading << SPIM_CONFIG_CPHA_Pos);
    NRF_SPIM1->ORC = 0u;
    NRF_SPIM1->ENABLE = SPIM_ENABLE_ENABLE_Enabled;

    if (!adxl362_write_reg(ADXL362_REG_SOFT_RESET, ADXL362_SOFT_RESET_KEY))
    {
        return false;
    }
    nrf_delay_ms(2u);
    if (!adxl362_read_regs(ADXL362_REG_DEVID_AD, id, sizeof(id)) ||
        (id[0] != ADXL362_DEVID_AD_VALUE) ||
        (id[2] != ADXL362_PARTID_VALUE))
    {
        NRF_LOG_ERROR("[ADXL362] ID error: %02x %02x %02x", id[0], id[1], id[2]);
        return false;
    }

    /* 关键安全设置：INT1接nRESET，两个INT都不允许映射任何事件。 */
    if (!adxl362_write_reg(ADXL362_REG_INTMAP1, 0x00u) ||
        !adxl362_write_reg(ADXL362_REG_INTMAP2, 0x00u) ||
        !adxl362_write_reg(ADXL362_REG_FILTER_CTL, ADXL362_FILTER_2G_100HZ) ||
        !adxl362_write_reg(ADXL362_REG_POWER_CTL, ADXL362_STANDBY_MODE))
    {
        return false;
    }
    m_ready = true;
    NRF_LOG_INFO("[ADXL362] ready: ID=%02x/%02x/%02x, 2g 100Hz, standby",
                 id[0], id[1], id[2]);
    return true;
}

bool adxl362_measurement_start(void)
{
    if (!m_ready ||
        !adxl362_write_reg(ADXL362_REG_POWER_CTL, ADXL362_MEASURE_MODE))
    {
        m_measuring = false;
        return false;
    }
    m_measuring = true;
    return true;
}

bool adxl362_read_sample(adxl362_sample_t *sample)
{
    uint8_t data[8];

    if (!m_ready || !m_measuring || (sample == NULL) ||
        !adxl362_read_regs(ADXL362_REG_XDATA_L, data, sizeof(data)))
    {
        return false;
    }
    sample->raw_x = (int16_t)((uint16_t)data[0] | ((uint16_t)data[1] << 8));
    sample->raw_y = (int16_t)((uint16_t)data[2] | ((uint16_t)data[3] << 8));
    sample->raw_z = (int16_t)((uint16_t)data[4] | ((uint16_t)data[5] << 8));
    sample->temperature_raw = (int16_t)((uint16_t)data[6] | ((uint16_t)data[7] << 8));

    return true;
}

bool adxl362_standby(void)
{
    bool success = m_ready &&
                   adxl362_write_reg(ADXL362_REG_POWER_CTL,
                                     ADXL362_STANDBY_MODE);
    m_measuring = false;
    return success;
}

void adxl362_log_raw_sample(uint16_t frame_id, const adxl362_sample_t *sample)
{
    if (sample == NULL)
    {
        NRF_LOG_WARNING("[ADXL362] frame=%u raw sample invalid", (unsigned)frame_id);
        return;
    }
    NRF_LOG_INFO("[ADXL362] frame=%u rawX=%d rawY=%d rawZ=%d rawT=%d",
                 (unsigned)frame_id, (int)sample->raw_x, (int)sample->raw_y,
                 (int)sample->raw_z, (int)sample->temperature_raw);
}
