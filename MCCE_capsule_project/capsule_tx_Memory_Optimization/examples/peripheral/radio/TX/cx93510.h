#ifndef CX93510_H
#define CX93510_H

/**
 * @file cx93510.h
 * @brief CX93510寄存器、传感器代理I2C、JPEG采集和帧缓冲接口。
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct
{
    /* 0x40块：包含SOI、DQT、DHT、EOI，正常长度为688字节。 */
    uint32_t config_offset;
    uint16_t config_size;
    /* 0x10块：包含SOI、SOF0、SOS、压缩图像数据、EOI。 */
    uint32_t jpeg_offset;
    uint16_t jpeg_size;
} cx93510_frame_info_t;

/**
 * @brief 初始化CX93510主机SPI、OV7676代理I2C、480x480 YUY2输入及JPEG编码器。
 */
bool cx93510_init(uint8_t *revision);
/** @brief 重新使能nRF侧SPIM0；CX93510帧RAM始终保持供电。 */
void cx93510_host_resume(void);
/** @brief 空闲时关闭nRF侧SPIM0，不改变P0.11帧RAM使能电平。 */
void cx93510_host_suspend(void);
/** @brief 通过CX93510内部I2C主机写一个OV7676的16位地址寄存器。 */
bool cx93510_sensor_write(uint16_t address, uint8_t value);
/** @brief 通过CX93510内部I2C主机读一个OV7676的16位地址寄存器。 */
bool cx93510_sensor_read(uint16_t address, uint8_t *value);
/**
 * @brief 复位压缩器、重新加载JPEG表、采集一帧并返回0x40/0x10块的位置。
 */
bool cx93510_capture_one(cx93510_frame_info_t *frame, uint32_t timeout_ms);
/** @brief 从CX93510内部帧缓冲的任意字节地址读取数据。 */
bool cx93510_frame_buffer_read(uint32_t byte_address, uint8_t *data, size_t length);

#endif
