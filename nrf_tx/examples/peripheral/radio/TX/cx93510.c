/**
 * @file cx93510.c
 * @brief CX93510 SPI控制、OV7676代理I2C、JPEG单帧采集和帧缓冲读取。
 */
#include "cx93510.h"
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
#define TX_INIT_LOG_FLUSH() ((void)0)
#else
#define TX_INIT_LOG_FLUSH() NRF_LOG_FLUSH()
#endif
#include "nrf_log_ctrl.h"
#include "spi_bus.h"

/* CX93510帧缓冲RAM电源控制脚，高电平允许RAM工作。 */
#define CX_PD_RAM_PIN       11u

/* JPEG控制/状态寄存器。 */
#define CX_REG_DIFF_JPEG    0x20u
#define CX_REG_JPEG_STATUS  0x21u
#define CX_REG_DEC_STAT3    0x2Bu
#define CX_REG_ENC_CTL1     0x26u
#define CX_REG_DCT_LUMA     0x27u
#define CX_REG_DCT_CHROMA   0x28u

/* 外部主机接口及内部帧缓冲访问寄存器。 */
#define CX_REG_SLAVE_SEL    0x50u
#define CX_REG_FB_ADDR_0    0x51u
#define CX_REG_FB_ADDR_1    0x52u
#define CX_REG_FB_ADDR_2    0x53u
#define CX_REG_ERROR_STATUS 0x54u
#define CX_REG_FB_DATA      0xCCu

/* 摄像头并行输入、裁剪尺寸和内部传感器I2C主机寄存器。 */
#define CX_REG_SI_CFG_1     0xA0u
#define CX_REG_SI_CFG_2     0xA1u
#define CX_REG_SI_CFG_3     0xA2u
#define CX_REG_H_ACTIVE     0xA3u
#define CX_REG_H_CAP_DELAY  0xA4u
#define CX_REG_H_CAP_WIDTH  0xA5u
#define CX_REG_V_CAP_DELAY  0xA7u
#define CX_REG_V_CAP_HEIGHT 0xA8u
#define CX_REG_I2C_DADDR    0xA9u
#define CX_REG_I2C_LO_ADDR  0xAAu
#define CX_REG_I2C_HI_ADDR  0xABu
#define CX_REG_I2C_LO_DATA  0xACu
#define CX_REG_I2C_CTL_1    0xAEu
#define CX_REG_I2C_CTL_2    0xAFu
#define CX_REG_I2C_CTL_3    0xB0u
#define CX_REG_PART_REV     0xFFu

#define CX_OV7676_ADDRESS   0x78u
/* 折中压缩档：亮度保留低压缩表0，色度使用高压缩表3。
 * 优先保留人眼更敏感的轮廓/纹理细节，同时减少色度数据和Radio占空时间。 */
#define CX_JPEG_DCT_LUMA    0x00u
#define CX_JPEG_DCT_CHROMA  0x03u
/* SPI读取命令占3字节，所以一次最多读取255-3=252字节数据。 */
#define CX_SPI_DATA_MAX     (SPI_BUS_MAX_TRANSFER - 3u)

/**
 * @brief 写一个CX93510 8位寄存器。
 * @note 写命令格式为80、寄存器地址、00、数据。
 */
static bool cx_write(uint8_t address, uint8_t value)
{
    uint8_t tx[4] = {0x80u, address, 0x00u, value};
    return spi_bus_transfer(tx, NULL, sizeof(tx));
}

/**
 * @brief 读一个CX93510 8位寄存器。
 * @note 先发送00、寄存器地址、00，再从第4个SPI字节取得返回数据。
 */
static bool cx_read(uint8_t address, uint8_t *value)
{
    uint8_t tx[4] = {0x00u, address, 0x00u, 0x00u};
    uint8_t rx[4];
    if ((value == NULL) || !spi_bus_transfer(tx, rx, sizeof(tx)))
    {
        return false;
    }
    *value = rx[3];
    return true;
}

/**
 * @brief 回读并校验旧版OV7676/CX93510配套采集参数。
 *
 * CX93510的宽高寄存器以8像素为单位，因此0x3C表示480像素。
 */
static bool cx_verify_camera_config(void)
{
    uint8_t si_cfg_1 = 0xFFu;
    uint8_t si_cfg_3 = 0xFFu;
    uint8_t h_active = 0xFFu;
    uint8_t h_delay = 0xFFu;
    uint8_t h_width = 0xFFu;
    uint8_t v_delay = 0xFFu;
    uint8_t v_height = 0xFFu;
    uint8_t dct_luma = 0xFFu;
    uint8_t dct_chroma = 0xFFu;
    uint8_t diff_jpeg = 0xFFu;
    uint8_t enc_ctl1 = 0xFFu;

    if (!cx_read(CX_REG_SI_CFG_1, &si_cfg_1) ||
        !cx_read(CX_REG_SI_CFG_3, &si_cfg_3) ||
        !cx_read(CX_REG_H_ACTIVE, &h_active) ||
        !cx_read(CX_REG_H_CAP_DELAY, &h_delay) ||
        !cx_read(CX_REG_H_CAP_WIDTH, &h_width) ||
        !cx_read(CX_REG_V_CAP_DELAY, &v_delay) ||
        !cx_read(CX_REG_V_CAP_HEIGHT, &v_height) ||
        !cx_read(CX_REG_DCT_LUMA, &dct_luma) ||
        !cx_read(CX_REG_DCT_CHROMA, &dct_chroma) ||
        !cx_read(CX_REG_DIFF_JPEG, &diff_jpeg) ||
        !cx_read(CX_REG_ENC_CTL1, &enc_ctl1))
    {
        NRF_LOG_ERROR("CX93510 camera-config readback failed");
        return false;
    }

    NRF_LOG_INFO("CX93510 readback: SI=%02x/%02x H=%02x/%02x/%02x",
                 si_cfg_1, si_cfg_3, h_active, h_delay, h_width);
    NRF_LOG_INFO("CX93510 readback: V=%02x/%02x",
                 v_delay, v_height);
    NRF_LOG_INFO("CX93510 readback: DCT=%02x/%02x DIFF_JPEG=%02x ENC_CTL1=%02x",
                 dct_luma, dct_chroma, diff_jpeg, enc_ctl1);
    TX_INIT_LOG_FLUSH();

    if ((si_cfg_1 != 0x24u) || (si_cfg_3 != 0x01u) ||
        (h_active != 0x00u) || (h_delay != 0x00u) ||
        (h_width != 0x3Cu) || (v_delay != 0x00u) ||
        (v_height != 0x3Cu) || (dct_luma != CX_JPEG_DCT_LUMA) ||
        (dct_chroma != CX_JPEG_DCT_CHROMA) || (diff_jpeg != 0x00u))
    {
        NRF_LOG_ERROR("CX93510 camera-config verification mismatch");
        return false;
    }
    return true;
}

/**
 * @brief 从同一CX93510地址进行SPI连续读取。
 * @note 对0xCC执行连续读取时，CX93510会依次输出帧缓冲数据。
 */
static bool cx_read_burst(uint8_t address, uint8_t *data, size_t length)
{
    uint8_t tx[SPI_BUS_MAX_TRANSFER];
    uint8_t rx[SPI_BUS_MAX_TRANSFER];
    size_t transfer_length = length + 3u;

    if ((data == NULL) || (length == 0u) || (length > CX_SPI_DATA_MAX))
    {
        return false;
    }

    memset(tx, 0, transfer_length);
    tx[1] = address;
    if (!spi_bus_transfer(tx, rx, transfer_length))
    {
        return false;
    }
    memcpy(data, &rx[3], length);
    return true;
}

/**
 * @brief 等待CX93510内部传感器I2C事务及写FIFO全部完成。
 * @return 传输结束且收到传感器ACK时返回true；超时/NACK返回false。
 */
static bool sensor_wait(uint32_t timeout_ms)
{
    uint8_t status;
    while (timeout_ms-- != 0u)
    {
        if (!cx_read(CX_REG_I2C_CTL_3, &status))
        {
            return false;
        }
        if (((status & 0x01u) == 0u) && ((status & 0x1Cu) == 0u))
        {
            return (status & 0x02u) != 0u;
        }
        nrf_delay_ms(1u);
    }
    return false;
}

/**
 * @brief 通过CX93510代理I2C写OV7676寄存器。
 *
 * 顺序为：等待空闲 -> 写16位子地址高/低字节 -> 设置写方向 ->
 * 写8位数据触发事务 -> 等待FIFO清空和ACK。
 */
bool cx93510_sensor_write(uint16_t address, uint8_t value)
{
    uint8_t control;
    if (!sensor_wait(20u) ||
        !cx_write(CX_REG_I2C_HI_ADDR, (uint8_t)(address >> 8)) ||
        !cx_write(CX_REG_I2C_LO_ADDR, (uint8_t)address) ||
        !cx_read(CX_REG_I2C_CTL_3, &control) ||
        !cx_write(CX_REG_I2C_CTL_3, (uint8_t)(control & (uint8_t)~0x20u)) ||
        !cx_write(CX_REG_I2C_LO_DATA, value))
    {
        NRF_LOG_ERROR("Sensor I2C write setup failed: reg=0x%04x value=0x%02x",
                      address, value);
        return false;
    }
    if (!sensor_wait(20u))
    {
        NRF_LOG_ERROR("Sensor I2C write timeout/NACK: reg=0x%04x", address);
        return false;
    }
    return true;
}

/**
 * @brief 通过CX93510代理I2C读OV7676寄存器。
 *
 * 写入16位子地址后，将I2C_CTL_3的READ位设置为1触发组合读事务，
 * 完成后从I2C_LO_DATA取得一个字节。
 */
bool cx93510_sensor_read(uint16_t address, uint8_t *value)
{
    uint8_t control;
    if ((value == NULL) || !sensor_wait(20u) ||
        !cx_write(CX_REG_I2C_HI_ADDR, (uint8_t)(address >> 8)) ||
        !cx_write(CX_REG_I2C_LO_ADDR, (uint8_t)address) ||
        !cx_read(CX_REG_I2C_CTL_3, &control) ||
        !cx_write(CX_REG_I2C_CTL_3, (uint8_t)((control & 0xC0u) | 0x20u)) ||
        !sensor_wait(20u))
    {
        NRF_LOG_ERROR("Sensor I2C read setup/timeout failed: reg=0x%04x", address);
        return false;
    }
    if (!cx_read(CX_REG_I2C_LO_DATA, value))
    {
        NRF_LOG_ERROR("Sensor I2C read-data failed: reg=0x%04x", address);
        return false;
    }
    return true;
}

/**
 * @brief 初始化CX93510。
 *
 * 主要步骤：
 * 1. 先初始化SPI并把HSS保持高电平，避免接口自动识别错误；
 * 2. P0.11拉高启用帧缓冲RAM；
 * 3. 发送一次会被自动识别逻辑丢弃的SPI事务，再读取芯片版本；
 * 4. 检查SLAVE_SEL低两位是否为01，确认芯片选择了SPI接口；
 * 5. 配置OV7676地址0x78、16位子地址、8位数据和约400 kHz I2C；
 * 6. 移植旧版同步极性、YUY2字节顺序和480x480采集窗口；
 * 7. 移植旧版JPEG亮度/色度量化表选择，并关闭差分JPEG。
 */
bool cx93510_init(uint8_t *revision)
{
    uint8_t id = 0xFFu;
    uint8_t interface_type = 0xFFu;

    /* Drive HSS high before CX93510 interface auto-detection can sample it. */
    spi_bus_init();
    nrf_gpio_cfg_output(CX_PD_RAM_PIN);
    nrf_gpio_pin_set(CX_PD_RAM_PIN);
    NRF_LOG_INFO("CX93510 PD_RAM enabled on P0.%02u", (unsigned)CX_PD_RAM_PIN);
    nrf_delay_ms(5u);
    NRF_LOG_INFO("CX93510 detect stage: sending SPI auto-detect transaction");
    TX_INIT_LOG_FLUSH();

    /* Auto-detection discards the first SPI transaction after power-up. */
    (void)cx_read(CX_REG_PART_REV, &id);
    nrf_delay_us(10u);
    if (!cx_read(CX_REG_PART_REV, &id) ||
        !cx_read(CX_REG_SLAVE_SEL, &interface_type) ||
        ((interface_type & 0x03u) != 0x01u) ||
        ((id >> 4) != 0u))
    {
        NRF_LOG_ERROR("CX93510 detect failed: PN_BO_REV=0x%02x SLAVE_SEL=0x%02x",
                      id, interface_type);
        return false;
    }
    NRF_LOG_INFO("CX93510 SPI detected: PN_BO_REV=0x%02x SLAVE_SEL=0x%02x",
                 id, interface_type);
    TX_INIT_LOG_FLUSH();

    if (revision != NULL)
    {
        *revision = id & 0x03u;
    }

    /* OV7676: 0x78/0x79，16位子地址、8位数据、约400kHz I2C。 */
    if (!cx_write(CX_REG_I2C_DADDR, CX_OV7676_ADDRESS) ||
        !cx_write(CX_REG_I2C_CTL_1, 0x11u) ||
        !cx_write(CX_REG_I2C_CTL_2, 0x90u) ||
        !cx_write(CX_REG_I2C_CTL_3, 0x00u) ||
        /*
         * 旧版SI_CFG_1=0x24：bit5设置垂直同步参考极性；
         * B_ORD[3:2]=01按Y0/Cb/Y1/Cr接收，与OV7676的YUY2匹配。
         */
        !cx_write(CX_REG_SI_CFG_1, 0x24u) ||
        /* 有限采集一帧；实际启动由cx93510_capture_one写SI_CFG_2。 */
        !cx_write(CX_REG_SI_CFG_3, 0x01u) ||
        !cx_write(CX_REG_H_ACTIVE, 0x00u) ||
        !cx_write(CX_REG_H_CAP_DELAY, 0x00u) ||
        !cx_write(CX_REG_H_CAP_WIDTH, 0x3Cu) ||
        !cx_write(CX_REG_V_CAP_DELAY, 0x00u) ||
        !cx_write(CX_REG_V_CAP_HEIGHT, 0x3Cu) ||
        /* 折中档：亮度表0保留细节，色度表3减少图像数据量。 */
        !cx_write(CX_REG_DCT_LUMA, CX_JPEG_DCT_LUMA) ||
        !cx_write(CX_REG_DCT_CHROMA, CX_JPEG_DCT_CHROMA) ||
        !cx_write(CX_REG_DIFF_JPEG, 0x00u))
    {
        NRF_LOG_ERROR("CX93510 sensor/JPEG register setup failed");
        return false;
    }
    /* 数据手册要求设置RELOAD_TABLES，编码器才会输出含DQT/DHT的0x40配置块。 */
    if (!cx_read(CX_REG_ENC_CTL1, &id) ||
        !cx_write(CX_REG_ENC_CTL1, (uint8_t)(id | 0x01u)) ||
        !cx_verify_camera_config())
    {
        NRF_LOG_ERROR("CX93510 JPEG reload/readback setup failed");
        return false;
    }
    NRF_LOG_INFO("CX93510 configured: 480x480 YUY2, direct JPEG, DCT=00/03");
    return true;
}

/**
 * @brief 复位JPEG压缩器和帧缓冲写指针。
 *
 * 设置RST_COMP后等待INIT_DONE，再清除复位并置位RELOAD_TABLES。
 * 每次新采集前执行，保证本帧数据从帧缓冲地址0开始。
 */
static bool compressor_reset(void)
{
    uint8_t value;
    uint32_t timeout = 100u;

    if (!cx_read(CX_REG_DIFF_JPEG, &value) ||
        !cx_write(CX_REG_DIFF_JPEG, (uint8_t)(value | 0x80u)))
    {
        NRF_LOG_ERROR("CX93510 compressor reset command failed");
        return false;
    }
    while (timeout-- != 0u)
    {
        if (!cx_read(CX_REG_DEC_STAT3, &value))
        {
            NRF_LOG_ERROR("CX93510 compressor status read failed");
            return false;
        }
        if ((value & 0x01u) != 0u)
        {
            break;
        }
        nrf_delay_ms(1u);
    }
    if ((value & 0x01u) == 0u)
    {
        NRF_LOG_ERROR("CX93510 compressor INIT_DONE timeout");
        return false;
    }
    /*
     * CX93510数据手册9.2.2/9.2.2说明：RST_COMP完成并清零后，必须将
     * JPEG_ENC_CTL1.RELOAD_TABLES(bit0)置1。这样帧缓冲首先生成包含
     * DQT/DHT的0x40配置块，随后才是包含SOF/SOS和图像数据的0x10块。
     */
    return cx_write(CX_REG_DIFF_JPEG, 0x00u) &&
           cx_read(CX_REG_ENC_CTL1, &value) &&
           cx_write(CX_REG_ENC_CTL1,
                     (uint8_t)(value | 0x01u));
}

/**
 * @brief 从CX93510帧缓冲读取任意一段数据。
 *
 * 字节地址先转换为64位QWORD地址和QWORD内偏移；随后写入FB_ADDR_0/1/2，
 * 等待PREFETCH_DONE，最后从特殊寄存器0xCC连续读出数据。
 */
bool cx93510_frame_buffer_read(uint32_t byte_address, uint8_t *data, size_t length)
{
    uint16_t qword_address;
    uint8_t request;
    uint32_t timeout = 20u;

    if ((data == NULL) || (length == 0u) || (length > CX_SPI_DATA_MAX) ||
        (byte_address > 0x3FFFFu))
    {
        NRF_LOG_ERROR("CX93510 FB read invalid: address=%u length=%u",
                      (unsigned)byte_address, (unsigned)length);
        return false;
    }

    qword_address = (uint16_t)(byte_address >> 3);
    request = (uint8_t)(0x10u | (byte_address & 0x07u));
    if (!cx_write(CX_REG_FB_ADDR_0, (uint8_t)(qword_address >> 8)) ||
        !cx_write(CX_REG_FB_ADDR_1, (uint8_t)qword_address) ||
        !cx_write(CX_REG_FB_ADDR_2, request))
    {
        NRF_LOG_ERROR("CX93510 FB request setup failed: address=%u",
                      (unsigned)byte_address);
        return false;
    }

    do
    {
        if (!cx_read(CX_REG_FB_ADDR_2, &request))
        {
            NRF_LOG_ERROR("CX93510 FB prefetch status read failed");
            return false;
        }
        if ((request & 0x20u) != 0u)
        {
            return cx_read_burst(CX_REG_FB_DATA, data, length);
        }
        nrf_delay_us(50u);
    } while (--timeout != 0u);
    NRF_LOG_ERROR("CX93510 FB prefetch timeout: address=%u",
                  (unsigned)byte_address);
    return false;
}

/**
 * @brief 扫描帧缓冲中的状态QWORD，定位JPEG配置块和JPEG图像块。
 *
 * 每块前8字节为状态QWORD：最高字节给出块类型，最后两字节给出有效长度；
 * 数据按8字节边界填充。配置块标志为0x40，JPEG帧标志为0x10。
 *
 * CX93510存在两种合法输出：
 * 1. 默认表模式：先0x40配置块，再0x10图像扫描块；
 * 2. 旧版产品模式：地址0直接出现0x10独立完整JPEG，不再产生0x40块。
 */
static bool find_frame_blocks(cx93510_frame_info_t *frame)
{
    uint8_t header[8];
    uint32_t address = 0u;
    uint32_t guard;

    memset(frame, 0, sizeof(*frame));
    for (guard = 0u; guard < 16u; ++guard)
    {
        uint16_t size;
        uint8_t flags;
        if (!cx93510_frame_buffer_read(address, header, sizeof(header)))
        {
            return false;
        }
        flags = header[0] & 0xF0u;
        size = (uint16_t)(((uint16_t)header[6] << 8) | header[7]);
        NRF_LOG_INFO("CX93510 FB block: address=%u flags=0x%02x size=%u",
                     (unsigned)address, flags, (unsigned)size);
        if (size == 0u)
        {
            break;
        }
        if ((flags & 0x40u) != 0u)
        {
            frame->config_offset = address + 8u;
            frame->config_size = size;
        }
        if ((flags & 0x10u) != 0u)
        {
            uint8_t jpeg_start[2];
            uint8_t jpeg_end[2];
            frame->jpeg_offset = address + 8u;
            frame->jpeg_size = size;
            if (frame->config_size != 0u)
            {
                return true;
            }

            /* 没有0x40块时，确认0x10块本身确实是完整JPEG。 */
            if ((size < 4u) ||
                !cx93510_frame_buffer_read(frame->jpeg_offset,
                                           jpeg_start, sizeof(jpeg_start)) ||
                !cx93510_frame_buffer_read(frame->jpeg_offset + size - 2u,
                                           jpeg_end, sizeof(jpeg_end)))
            {
                NRF_LOG_ERROR("CX93510 standalone JPEG marker read failed");
                return false;
            }
            NRF_LOG_INFO("CX93510 standalone JPEG markers: %02x%02x ... %02x%02x",
                         jpeg_start[0], jpeg_start[1], jpeg_end[0], jpeg_end[1]);
            if ((jpeg_start[0] != 0xFFu) || (jpeg_start[1] != 0xD8u) ||
                (jpeg_end[0] != 0xFFu) || (jpeg_end[1] != 0xD9u))
            {
                NRF_LOG_ERROR("CX93510 standalone JPEG SOI/EOI mismatch");
                return false;
            }
            return true;
        }
        address += 8u + ((size + 7u) & ~7u);
    }
    return false;
}

/**
 * @brief 等待JPEG控制器把第一个状态QWORD真正提交到帧缓冲。
 *
 * SI_CFG_2.EN_LFC由采集前端在收到规定帧数后清零，但JPEG编码器和RAM
 * 写入管线可能还需要几毫秒。若此时马上读地址0，偶尔会读到全零块头。
 * 本函数只探测第一个8字节状态QWORD，直到块类型和长度同时有效。
 */
static bool wait_frame_buffer_header(uint32_t timeout_ms)
{
    uint8_t header[8];
    bool waiting_logged = false;

    while (timeout_ms-- != 0u)
    {
        uint8_t flags;
        uint16_t size;
        if (!cx93510_frame_buffer_read(0u, header, sizeof(header)))
        {
            NRF_LOG_ERROR("CX93510 frame-header probe read failed");
            return false;
        }

        flags = header[0] & 0xF0u;
        size = (uint16_t)(((uint16_t)header[6] << 8) | header[7]);
        if ((size != 0u) &&
            (((flags & 0x40u) != 0u) || ((flags & 0x10u) != 0u)))
        {
            if (waiting_logged)
            {
                NRF_LOG_INFO("CX93510 frame buffer committed: flags=0x%02x size=%u",
                             flags, (unsigned)size);
            }
            return true;
        }

        if (!waiting_logged)
        {
            NRF_LOG_INFO("CX93510 capture frontend done; waiting for JPEG/RAM commit");
            waiting_logged = true;
        }
        nrf_delay_ms(1u);
    }

    NRF_LOG_ERROR("CX93510 JPEG/RAM commit timeout");
    return false;
}

/**
 * @brief 触发并等待一次JPEG单帧采集。
 *
 * 先复位压缩器，设置FRAME_NUM=1并启用有限帧模式；
 * SI_CFG_2的EN_LFC位由硬件在一帧完成后自动清零；
 * 完成后扫描帧缓冲，超时时额外读取CX和OV7676关键寄存器用于定位问题。
 */
bool cx93510_capture_one(cx93510_frame_info_t *frame, uint32_t timeout_ms)
{
    uint8_t status;
    NRF_LOG_INFO("CX93510 capture stage: reset compressor/frame buffer");
    TX_INIT_LOG_FLUSH();
    if ((frame == NULL) || !compressor_reset() ||
        !cx_write(CX_REG_SI_CFG_3, 0x01u) ||
        !cx_write(CX_REG_SI_CFG_2, 0x80u))
    {
        NRF_LOG_ERROR("CX93510 single-frame capture start failed");
        return false;
    }
    NRF_LOG_INFO("CX93510 capture stage: waiting for one frame");
    TX_INIT_LOG_FLUSH();

    while (timeout_ms-- != 0u)
    {
        if (!cx_read(CX_REG_SI_CFG_2, &status))
        {
            NRF_LOG_ERROR("CX93510 capture status read failed");
            return false;
        }
        if ((status & 0x80u) == 0u)
        {
            if ((status & 0x10u) != 0u)
            {
                NRF_LOG_ERROR("CX93510 capture stopped: embedded-code error, status=0x%02x",
                              status);
                return false;
            }
            NRF_LOG_INFO("CX93510 capture frontend complete; checking frame buffer");
            if (!wait_frame_buffer_header(100u))
            {
                return false;
            }
            NRF_LOG_INFO("CX93510 JPEG committed; scanning frame buffer");
            if (!find_frame_blocks(frame))
            {
                NRF_LOG_ERROR("CX93510 JPEG config/image blocks not found");
                return false;
            }
            return true;
        }
        nrf_delay_ms(1u);
    }
    {
        uint8_t error_status = 0xFFu;
        uint8_t jpeg_status = 0xFFu;
        uint8_t sensor_mode = 0xFFu;
        uint8_t sensor_sync_oen = 0xFFu;
        uint8_t sensor_data_oen = 0xFFu;
        uint8_t sensor_format = 0xFFu;
        uint8_t sensor_frame_count = 0xFFu;

        (void)cx_read(CX_REG_ERROR_STATUS, &error_status);
        (void)cx_read(CX_REG_JPEG_STATUS, &jpeg_status);
        (void)cx93510_sensor_read(0x0100u, &sensor_mode);
        (void)cx93510_sensor_read(0x3001u, &sensor_sync_oen);
        (void)cx93510_sensor_read(0x3002u, &sensor_data_oen);
        (void)cx93510_sensor_read(0x4308u, &sensor_format);
        (void)cx93510_sensor_read(0x4A00u, &sensor_frame_count);

        NRF_LOG_ERROR("CX93510 capture timeout: SI_CFG_2=0x%02x ERR=0x%02x JPEG=0x%02x",
                      status, error_status, jpeg_status);
        NRF_LOG_ERROR("OV7676 state: MODE=0x%02x SYNC_OEN=0x%02x DATA_OEN=0x%02x FMT=0x%02x FRAME=%u",
                      sensor_mode, sensor_sync_oen, sensor_data_oen,
                      sensor_format, (unsigned)sensor_frame_count);
        TX_INIT_LOG_FLUSH();
    }
    return false;
}
