#ifndef DEV_ADXL362_H
#define DEV_ADXL362_H

#include <stdbool.h>
#include <stdint.h>

/** ADXL362在+-2 g量程下的原始数据和换算后的mg数据。 */
typedef struct
{
    int16_t raw_x;
    int16_t raw_y;
    int16_t raw_z;
    int16_t temperature_raw;
} adxl362_sample_t;

/** 初始化SPIM1和ADXL362。INT1/INT2均保持高阻，不使用中断。 */
bool adxl362_init(void);

/** 从standby进入100Hz测量；应在图像预热开始时调用。 */
bool adxl362_measurement_start(void);

/** 读取一次XYZ和温度原始值。 */
bool adxl362_read_sample(adxl362_sample_t *sample);

/** 停止测量并进入standby；SPI配置和寄存器内容保持。 */
bool adxl362_standby(void);

/** 输出与某一图像帧对应的XYZ原始寄存器值，不做单位或姿态换算。 */
void adxl362_log_raw_sample(uint16_t frame_id, const adxl362_sample_t *sample);

#endif
