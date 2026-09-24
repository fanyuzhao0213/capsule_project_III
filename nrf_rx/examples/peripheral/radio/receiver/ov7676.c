/**
 * @file ov7676.c
 * @brief 通过CX93510内部I2C主机配置OV7676图像传感器。
 *
 * 使用旧产品中已经验证过的OV7676完整寄存器表。旧驱动的裸寄存器
 * 写入被替换为当前工程带ACK和超时检查的代理I2C接口。
 */
#include "ov7676.h"

#include "cx93510.h"
#include "nrf_delay.h"
#include "nrf_gpio.h"
#include "nrf_log.h"
#include "nrf_log_ctrl.h"

/* OV7676硬件关断脚由nRF52832 P0.06控制，高电平为正常工作。 */
#define OV7676_XSHUTDOWN_PIN 6u

#define OV_REG_MODE_SELECT   0x0100u
#define OV_REG_CHIP_ID_H     0x300Au
#define OV_REG_CHIP_ID_L     0x300Bu
#define OV_REG_REVISION      0x302Au
#define OV_REG_IO_CTRL       0x3001u
#define OV_REG_DVP_DATA_CTRL 0x3002u
#define OV_REG_WIDTH_H       0x3808u
#define OV_REG_WIDTH_L       0x3809u
#define OV_REG_HEIGHT_H      0x380Au
#define OV_REG_HEIGHT_L      0x380Bu
#define OV_REG_FORMAT        0x4308u
#define OV_REG_DVP_POLARITY  0x4708u
#define OV_REG_YUV_ORDER     0x5009u

typedef struct
{
    uint16_t address;                                  /** OV7676寄存器地址。 */
    uint8_t value;                                     /** 写入寄存器的配置值。 */
    uint8_t delay_ms;                                  /** 写入该寄存器后的等待时间，单位ms。 */
} ov7676_reg_t;

/**
 * @brief 旧版产品使用的OV7676 480x480 YUV422完整配置表。
 *
 * 0x3808..0x380B设置输出为480x480；0x4308=0x04选择YUV422。
 * 原代码在0x5804..0x580A连续区域中写了0x2809，按上下文修正为0x5809。
 * 0x5009在表后单独写0，明确输出Y0/U/Y1/V顺序。
 */
static const ov7676_reg_t m_ov7676_480x480_yuv422[] =
{
    /* 软件复位、芯片基础控制、DVP同步/数据输出使能和内部时钟。 */
    {0x0103u, 0x01u, 5u},
    {0x0001u, 0xADu, 0u},
    {0x0002u, 0xADu, 0u},
    {0x3001u, 0x07u, 0u},
    {0x3002u, 0xFFu, 0u},
    {0x301Eu, 0x09u, 0u},
    {0x3080u, 0x02u, 0u},
    {0x3081u, 0x10u, 0u},
    {0x3082u, 0x01u, 0u},
    {0x3083u, 0x01u, 0u},
    {0x3084u, 0x01u, 0u},
    {0x3103u, 0x01u, 0u},
    {0x3503u, 0x00u, 0u},
    {0x3600u, 0x03u, 0u},
    {0x3602u, 0x0Eu, 1u},
    {0x3612u, 0x0Du, 0u},
    {0x3614u, 0x53u, 0u},
    {0x37C0u, 0x07u, 0u},

    /*
     * 有效像素窗口、输出尺寸、行/帧总周期以及水平/垂直起始偏移。
     * 0x3808/09=0x01E0、0x380A/0B=0x01E0，即最终输出480x480。
     */
    {0x3800u, 0x00u, 0u},
    {0x3801u, 0x50u, 0u},
    {0x3802u, 0x00u, 0u},
    {0x3803u, 0x00u, 0u},
    {0x3804u, 0x02u, 0u},
    {0x3805u, 0x37u, 0u},
    {0x3806u, 0x01u, 0u},
    {0x3807u, 0xE7u, 0u},
    {0x3808u, 0x01u, 0u},
    {0x3809u, 0xE0u, 0u},
    {0x380Au, 0x01u, 0u},
    {0x380Bu, 0xE0u, 1u},
    {0x380Cu, 0x02u, 0u},
    {0x380Du, 0x87u, 0u},
    {0x380Eu, 0x02u, 0u},
    {0x380Fu, 0x32u, 0u},
    {0x3810u, 0x00u, 0u},
    {0x3811u, 0x04u, 0u},
    {0x3812u, 0x00u, 0u},
    {0x3813u, 0x04u, 0u},
    {0x3820u, 0x10u, 0u},
    {0x3821u, 0x00u, 0u},

    /* 自动曝光/自动增益相关参数，沿用量产配置保证亮度收敛特性一致。 */
    {0x3A00u, 0x51u, 0u},
    {0x3A01u, 0x05u, 0u},
    {0x3A02u, 0x7Fu, 0u},
    {0x3A03u, 0x28u, 0u},
    {0x3A04u, 0x26u, 0u},
    {0x3A06u, 0x00u, 0u},
    {0x3A07u, 0xA1u, 0u},
    {0x3A08u, 0x00u, 0u},
    {0x3A09u, 0x86u, 0u},
    {0x3A0Eu, 0x01u, 1u},
    {0x3A0Fu, 0xE3u, 0u},
    {0x3A10u, 0x01u, 0u},
    {0x3A11u, 0xFCu, 0u},

    /* 黑电平、坏点/噪声处理和各颜色通道校正参数。 */
    {0x4008u, 0x00u, 0u},
    {0x4009u, 0x01u, 0u},
    {0x4011u, 0xF0u, 0u},
    {0x4013u, 0x05u, 0u},
    {0x4014u, 0x05u, 0u},
    {0x4015u, 0x05u, 0u},
    {0x4017u, 0x08u, 0u},

    /* DVP/YUV输出格式：0x4308=0x04选择YUV422并送往CX93510。 */
    {0x4300u, 0x03u, 0u},
    {0x4301u, 0xFFu, 0u},
    {0x4304u, 0x03u, 0u},
    {0x4305u, 0xFFu, 0u},
    {0x4308u, 0x04u, 1u},

    /* ISP总开关、颜色处理、YUV路径和后处理功能。 */
    {0x4F00u, 0x80u, 0u},
    {0x4F01u, 0x10u, 0u},
    {0x4F02u, 0x00u, 0u},
    {0x5000u, 0xFFu, 0u},
    {0x5001u, 0x3Fu, 0u},
    {0x5002u, 0x48u, 0u},
    {0x5007u, 0x2Fu, 0u},
    {0x5080u, 0x00u, 0u},

    /* 自动白平衡、颜色增益及量产标定参数。 */
    {0x5200u, 0x14u, 0u},
    {0x5201u, 0x02u, 0u},
    {0x5202u, 0x02u, 0u},
    {0x5203u, 0x53u, 0u},
    {0x5204u, 0x03u, 0u},
    {0x5205u, 0xBEu, 0u},
    {0x5206u, 0x00u, 0u},
    {0x5207u, 0x7Au, 0u},
    {0x5208u, 0x03u, 0u},
    {0x5209u, 0xE8u, 0u},
    {0x520Au, 0x00u, 0u},
    {0x520Bu, 0x9Au, 1u},
    {0x5210u, 0x02u, 0u},
    {0x5211u, 0x02u, 0u},
    {0x5228u, 0x03u, 0u},
    {0x5229u, 0xE4u, 0u},
    {0x522Cu, 0x00u, 0u},
    {0x522Du, 0x68u, 0u},
    {0x522Au, 0x00u, 0u},
    {0x522Bu, 0x00u, 0u},
    {0x522Eu, 0x00u, 0u},
    {0x522Fu, 0x88u, 0u},
    {0x5230u, 0x24u, 0u},

    /* 色彩/饱和度相关参数。 */
    {0x550Du, 0x00u, 0u},
    {0x5500u, 0x10u, 0u},
    {0x5501u, 0x24u, 0u},
    {0x5502u, 0x22u, 1u},
    {0x5503u, 0x07u, 0u},
    {0x5504u, 0x10u, 0u},
    {0x5505u, 0x10u, 0u},
    {0x5506u, 0x10u, 0u},
    {0x5507u, 0x10u, 0u},
    {0x5509u, 0x10u, 0u},
    {0x550Au, 0x24u, 0u},
    {0x5600u, 0x48u, 0u},

    /* 镜头阴影/边缘区域补偿参数；0x5809为对旧表明显笔误0x2809的修正。 */
    {0x5804u, 0x28u, 0u},
    {0x5805u, 0x11u, 0u},
    {0x5806u, 0x1Du, 0u},
    {0x5807u, 0x25u, 0u},
    {0x5809u, 0x10u, 0u},
    {0x580Au, 0x40u, 0u}
};

/** @brief 顺序写入完整配置表，任意寄存器失败都会停止初始化。 */
static bool ov7676_write_table(void)
{
    uint16_t i;
    uint16_t count = (uint16_t)(sizeof(m_ov7676_480x480_yuv422) /
                                sizeof(m_ov7676_480x480_yuv422[0]));

    NRF_LOG_INFO("OV7676 config stage: writing %u production registers",
                 (unsigned)count);
    NRF_LOG_FLUSH();
    for (i = 0u; i < count; ++i)
    {
        const ov7676_reg_t *entry = &m_ov7676_480x480_yuv422[i];
        if (!cx93510_sensor_write(entry->address, entry->value))
        {
            NRF_LOG_ERROR("OV7676 config failed: index=%u reg=0x%04x value=0x%02x",
                          (unsigned)i, entry->address, entry->value);
            NRF_LOG_FLUSH();
            return false;
        }
        if (entry->delay_ms != 0u)
        {
            nrf_delay_ms(entry->delay_ms);
        }
        nrf_delay_us(200u);
    }
    return true;
}

/** @brief 回读并校验决定输出格式和尺寸的关键寄存器。 */
static bool ov7676_verify_output(void)
{
    uint8_t mode = 0xFFu;
    uint8_t io_ctrl = 0xFFu;
    uint8_t data_ctrl = 0xFFu;
    uint8_t width_h = 0xFFu;
    uint8_t width_l = 0xFFu;
    uint8_t height_h = 0xFFu;
    uint8_t height_l = 0xFFu;
    uint8_t format = 0xFFu;
    uint8_t yuv_order = 0xFFu;
    uint8_t polarity = 0xFFu;

    if (!cx93510_sensor_read(OV_REG_MODE_SELECT, &mode) ||
        !cx93510_sensor_read(OV_REG_IO_CTRL, &io_ctrl) ||
        !cx93510_sensor_read(OV_REG_DVP_DATA_CTRL, &data_ctrl) ||
        !cx93510_sensor_read(OV_REG_WIDTH_H, &width_h) ||
        !cx93510_sensor_read(OV_REG_WIDTH_L, &width_l) ||
        !cx93510_sensor_read(OV_REG_HEIGHT_H, &height_h) ||
        !cx93510_sensor_read(OV_REG_HEIGHT_L, &height_l) ||
        !cx93510_sensor_read(OV_REG_FORMAT, &format) ||
        !cx93510_sensor_read(OV_REG_YUV_ORDER, &yuv_order) ||
        !cx93510_sensor_read(OV_REG_DVP_POLARITY, &polarity))
    {
        NRF_LOG_ERROR("OV7676 critical-register readback failed");
        return false;
    }

    NRF_LOG_INFO("OV7676 readback: MODE=%02x OEN=%02x/%02x SIZE=%ux%u",
                 mode, io_ctrl, data_ctrl,
                 (unsigned)(((uint16_t)width_h << 8) | width_l),
                 (unsigned)(((uint16_t)height_h << 8) | height_l));
    NRF_LOG_INFO("OV7676 readback: FORMAT=%02x YUV_ORDER=%02x POLARITY=%02x",
                 format, yuv_order, polarity);
    NRF_LOG_FLUSH();

    if ((mode != 0x01u) || (io_ctrl != 0x07u) || (data_ctrl != 0xFFu) ||
        (width_h != 0x01u) || (width_l != 0xE0u) ||
        (height_h != 0x01u) || (height_l != 0xE0u) ||
        (format != 0x04u) || (yuv_order != 0x00u))
    {
        NRF_LOG_ERROR("OV7676 critical-register verification mismatch");
        return false;
    }
    return true;
}

/** @brief 完成硬件唤醒、身份检查、完整配置和视频流启动。 */
bool ov7676_init(uint16_t *chip_id, uint8_t *revision)
{
    uint8_t id_h = 0u;
    uint8_t id_l = 0u;
    uint8_t rev = 0u;

    nrf_gpio_cfg_output(OV7676_XSHUTDOWN_PIN);
    nrf_gpio_pin_clear(OV7676_XSHUTDOWN_PIN);
    NRF_LOG_INFO("OV7676 XSHUTDOWN asserted on P0.%02u",
                 (unsigned)OV7676_XSHUTDOWN_PIN);
    nrf_delay_ms(5u);
    nrf_gpio_pin_set(OV7676_XSHUTDOWN_PIN);
    NRF_LOG_INFO("OV7676 XSHUTDOWN released; waiting for XVCLK/I2C");
    NRF_LOG_FLUSH();
    nrf_delay_ms(5u);

    /* 先读ID确认总线和器件正确，再执行包含软件复位的完整旧版表。 */
    if (!cx93510_sensor_read(OV_REG_CHIP_ID_H, &id_h) ||
        !cx93510_sensor_read(OV_REG_CHIP_ID_L, &id_l) ||
        !cx93510_sensor_read(OV_REG_REVISION, &rev) ||
        (id_h != 0x76u) || (id_l != 0x76u))
    {
        NRF_LOG_ERROR("OV7676 ID read/check failed: high=0x%02x low=0x%02x",
                      id_h, id_l);
        return false;
    }
    NRF_LOG_INFO("OV7676 ID OK: 0x%02x%02x, revision=0x%02x",
                 id_h, id_l, rev);
    NRF_LOG_FLUSH();

    if (!ov7676_write_table())
    {
        return false;
    }

    /*
     * 显式锁定YUY2顺序，再启动连续视频输出：
     * 每两个像素按Y0/U/Y1/V输出，CX93510的SI_CFG_1也必须配置为相同顺序。
     */
    if (!cx93510_sensor_write(OV_REG_YUV_ORDER, 0x00u) ||
        !cx93510_sensor_write(OV_REG_MODE_SELECT, 0x01u))
    {
        NRF_LOG_ERROR("OV7676 stream start/YUV order setup failed");
        return false;
    }

    /* 旧驱动在完整表写完后等待60ms，让AE/AWB/ISP开始稳定。 */
    nrf_delay_ms(60u);
    if (!ov7676_verify_output())
    {
        return false;
    }
    NRF_LOG_INFO("OV7676 streaming: production table, 480x480 YUV422 Y0/U/Y1/V");

    if (chip_id != NULL)
    {
        *chip_id = (uint16_t)(((uint16_t)id_h << 8) | id_l);
    }
    if (revision != NULL)
    {
        *revision = rev;
    }
    return true;
}
