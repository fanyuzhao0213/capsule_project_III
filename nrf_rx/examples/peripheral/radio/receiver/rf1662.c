/**
 * @file rf1662.c
 * @brief RF1662SR 两线控制和 12 路天线选择实现。
 *
 * RF1662 的公开资料较少。本实现严格移植旧 TYGD31 RX 工程已经验证过的
 * 启动顺序、奇校验时序和天线选择编码。
 */

#include "rf1662.h"
#include "config.h"
#include "nrf_delay.h"
#include "nrf_gpio.h"

/** RF1662 从设备地址。 */
#define RF1662_SLAVE_ADDRESS       0x0Au

/** 电源模式/触发配置寄存器。 */
#define RF1662_PM_TRIG_ADDRESS     0x001Cu

/** 电源模式值。 */
#define RF1662_PM_STARTUP          0x40u
#define RF1662_PM_ACTIVE           0x00u

/** 不使用外部触发脚，写入后直接切换。 */
#define RF1662_TRIGGER_IMMEDIATE   0x38u

/** 旧工程使用的基本位时序。 */
#define RF1662_BIT_DELAY_US        2u
#define RF1662_START_DELAY_US      (RF1662_BIT_DELAY_US * 6u)

/** ANT1~ANT12 对应的 RF1662 Register 0 编码。 */
static const uint8_t m_antenna_codes[RF1662_ANTENNA_COUNT] =
{
    0x02u, 0x0Au, 0x0Eu, 0x0Bu,
    0x01u, 0x09u, 0x06u, 0x04u,
    0x0Cu, 0x08u, 0x03u, 0x05u
};

/** 当前天线下标。 */
static uint8_t m_current_antenna;

/** @brief 初始化串行控制 GPIO。 */
static void rf1662_gpio_init(void)
{
    nrf_gpio_cfg_output(RF1662_SCLK_PIN);
    nrf_gpio_cfg_output(RF1662_SDATA_PIN);
    nrf_gpio_pin_clear(RF1662_SCLK_PIN);
    nrf_gpio_pin_clear(RF1662_SDATA_PIN);
}

/** @brief 产生旧工程定义的 RF1662 帧起始序列。 */
static void rf1662_start(void)
{
    nrf_gpio_cfg_output(RF1662_SDATA_PIN);
    nrf_gpio_pin_clear(RF1662_SDATA_PIN);
    nrf_delay_us(RF1662_START_DELAY_US);
    nrf_gpio_pin_set(RF1662_SDATA_PIN);
    nrf_delay_us(RF1662_START_DELAY_US);
    nrf_gpio_pin_clear(RF1662_SDATA_PIN);
    nrf_delay_us(RF1662_START_DELAY_US);
}

/** @brief 产生旧工程定义的 RF1662 帧结束序列。 */
static void rf1662_stop(void)
{
    nrf_gpio_cfg_output(RF1662_SDATA_PIN);
    nrf_gpio_pin_clear(RF1662_SDATA_PIN);
    nrf_gpio_pin_set(RF1662_SCLK_PIN);
    nrf_delay_us(RF1662_BIT_DELAY_US);
    nrf_gpio_pin_clear(RF1662_SCLK_PIN);
    nrf_delay_us(RF1662_BIT_DELAY_US);
}

/** @brief 按旧工程时序发送一位。 */
static void rf1662_write_bit(uint8_t value)
{
    nrf_gpio_pin_set(RF1662_SCLK_PIN);
    if (value != 0u)
    {
        nrf_gpio_pin_set(RF1662_SDATA_PIN);
    }
    else
    {
        nrf_gpio_pin_clear(RF1662_SDATA_PIN);
    }
    nrf_delay_us(RF1662_BIT_DELAY_US);
    nrf_gpio_pin_clear(RF1662_SCLK_PIN);
    nrf_delay_us(RF1662_BIT_DELAY_US);
}

/** @brief 发送奇校验位，使此前所有 1 的数量加校验位后为奇数。 */
static void rf1662_write_odd_parity(uint8_t ones)
{
    rf1662_write_bit((uint8_t)(((ones & 1u) == 0u) ? 1u : 0u));
}

/** @brief 发送扩展寄存器写命令的命令头。 */
static void rf1662_write_register_header(uint8_t slave_address,
                                         uint16_t register_address)
{
    uint8_t i;
    uint8_t ones = 0u;

    for (i = 0u; i < 4u; ++i)
    {
        uint8_t bit = (uint8_t)((slave_address & 0x08u) != 0u);
        ones = (uint8_t)(ones + bit);
        rf1662_write_bit(bit);
        slave_address <<= 1;
    }

    /* 旧协议的扩展寄存器写标志：0, 1, 0。 */
    rf1662_write_bit(0u);
    rf1662_write_bit(1u);
    ++ones;
    rf1662_write_bit(0u);

    for (i = 0u; i < 5u; ++i)
    {
        uint8_t bit = (uint8_t)((register_address & 0x10u) != 0u);
        ones = (uint8_t)(ones + bit);
        rf1662_write_bit(bit);
        register_address <<= 1;
    }
    rf1662_write_odd_parity(ones);
}

/** @brief 发送一个带奇校验的寄存器数据字节。 */
static void rf1662_write_data(uint8_t data)
{
    uint8_t i;
    uint8_t ones = 0u;

    for (i = 0u; i < 8u; ++i)
    {
        uint8_t bit = (uint8_t)((data & 0x80u) != 0u);
        ones = (uint8_t)(ones + bit);
        rf1662_write_bit(bit);
        data <<= 1;
    }
    rf1662_write_odd_parity(ones);
}

/** @brief 写 RF1662 Register 0，用于立即选择天线通道。 */
static void rf1662_write_register_zero(uint8_t slave_address, uint8_t data)
{
    uint8_t i;
    uint8_t ones = 0u;

    for (i = 0u; i < 4u; ++i)
    {
        uint8_t bit = (uint8_t)((slave_address & 0x08u) != 0u);
        ones = (uint8_t)(ones + bit);
        rf1662_write_bit(bit);
        slave_address <<= 1;
    }

    /* Register 0 write flag。 */
    rf1662_write_bit(1u);
    ++ones;

    for (i = 0u; i < 7u; ++i)
    {
        uint8_t bit = (uint8_t)((data & 0x40u) != 0u);
        ones = (uint8_t)(ones + bit);
        rf1662_write_bit(bit);
        data <<= 1;
    }
    rf1662_write_odd_parity(ones);
}

/** @brief 写一个完整的扩展寄存器。 */
static void rf1662_write_register(uint16_t address, uint8_t data)
{
    rf1662_start();
    rf1662_write_register_header(RF1662_SLAVE_ADDRESS, address);
    rf1662_write_data(data);
    rf1662_stop();
}

bool rf1662_select_antenna(uint8_t antenna_index)
{
    if (antenna_index >= RF1662_ANTENNA_COUNT)
    {
        return false;
    }

    rf1662_start();
    rf1662_write_register_zero(RF1662_SLAVE_ADDRESS,
                               m_antenna_codes[antenna_index]);
    rf1662_stop();
    m_current_antenna = antenna_index;
    return true;
}

bool rf1662_init(uint8_t antenna_index)
{
    if (antenna_index >= RF1662_ANTENNA_COUNT)
    {
        return false;
    }

    rf1662_gpio_init();
    nrf_delay_us(20u);

    /* 旧工程要求：先进入 STARTUP，再进入 ACTIVE。 */
    rf1662_write_register(RF1662_PM_TRIG_ADDRESS, RF1662_PM_STARTUP);
    nrf_delay_us(10u);
    rf1662_write_register(RF1662_PM_TRIG_ADDRESS, RF1662_PM_ACTIVE);

    /* 选择立即生效模式，不依赖外部触发脚。 */
    rf1662_write_register(RF1662_PM_TRIG_ADDRESS,
                          RF1662_TRIGGER_IMMEDIATE);
    nrf_delay_us(1000u);

    return rf1662_select_antenna(antenna_index);
}

uint8_t rf1662_get_antenna(void)
{
    return m_current_antenna;
}
