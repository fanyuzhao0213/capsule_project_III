/**
 ******************************************************************************
 * @file    sd2058.c
 * @brief   使用PB6/PB7软件I2C访问SD2058 RTC，并处理PC下发的BCD时间。
 *
 * 数据顺序：
 *   PC命令时间[年,月,日,星期,时,分,秒]
 *       ↔ SD2058寄存器[秒,分,时,星期,日,月,年]
 ******************************************************************************
 */

#include "sd2058.h"
#include "stm32f1xx_hal.h"

#define SD2058_PORT       GPIOB
#define SD2058_SCL_PIN    GPIO_PIN_6
#define SD2058_SDA_PIN    GPIO_PIN_7
#define SD2058_WRITE_ADDR 0x64U
#define SD2058_READ_ADDR  0x65U

static void Sd2058_Delay(void)
{
  volatile uint32_t count;
  for (count = 0U; count < 24U; count++)
  {
    __NOP();
  }
}

static void Sd2058_Scl(uint8_t high)
{
  HAL_GPIO_WritePin(SD2058_PORT, SD2058_SCL_PIN,
                    high ? GPIO_PIN_SET : GPIO_PIN_RESET);
  Sd2058_Delay();
}

static void Sd2058_Sda(uint8_t high)
{
  HAL_GPIO_WritePin(SD2058_PORT, SD2058_SDA_PIN,
                    high ? GPIO_PIN_SET : GPIO_PIN_RESET);
  Sd2058_Delay();
}

static uint8_t Sd2058_Start(void)
{
  Sd2058_Sda(1U);
  Sd2058_Scl(1U);
  if (HAL_GPIO_ReadPin(SD2058_PORT, SD2058_SDA_PIN) == GPIO_PIN_RESET)
  {
    return 0U;
  }
  Sd2058_Sda(0U);
  Sd2058_Scl(0U);
  return 1U;
}

static void Sd2058_Stop(void)
{
  Sd2058_Scl(0U);
  Sd2058_Sda(0U);
  Sd2058_Scl(1U);
  Sd2058_Sda(1U);
}

static uint8_t Sd2058_WriteByte(uint8_t value)
{
  uint8_t bit;
  uint8_t acknowledged;

  for (bit = 0U; bit < 8U; bit++)
  {
    Sd2058_Scl(0U);
    Sd2058_Sda((value & 0x80U) != 0U);
    Sd2058_Scl(1U);
    value <<= 1;
  }
  Sd2058_Scl(0U);
  Sd2058_Sda(1U);
  Sd2058_Scl(1U);
  acknowledged = (HAL_GPIO_ReadPin(SD2058_PORT, SD2058_SDA_PIN) ==
                  GPIO_PIN_RESET) ? 1U : 0U;
  Sd2058_Scl(0U);
  return acknowledged;
}

static uint8_t Sd2058_ReadByte(uint8_t acknowledge)
{
  uint8_t bit;
  uint8_t value = 0U;

  Sd2058_Sda(1U);
  for (bit = 0U; bit < 8U; bit++)
  {
    Sd2058_Scl(0U);
    Sd2058_Scl(1U);
    value = (uint8_t)(value << 1);
    if (HAL_GPIO_ReadPin(SD2058_PORT, SD2058_SDA_PIN) == GPIO_PIN_SET)
    {
      value |= 1U;
    }
  }
  Sd2058_Scl(0U);
  Sd2058_Sda(acknowledge ? 0U : 1U);
  Sd2058_Scl(1U);
  Sd2058_Scl(0U);
  Sd2058_Sda(1U);
  return value;
}

/**
 * @brief 初始化SD2058软件I2C引脚并读取一次时间确认器件可用。
 *
 * 简易流程图：
 *   开GPIOB时钟
 *       → PB6/PB7配置为开漏输出
 *       → SCL/SDA释放为高
 *       → 连续读取7字节时间
 *       → 全部I2C步骤ACK则返回1，否则返回0
 */
uint8_t Sd2058_Init(void)
{
  GPIO_InitTypeDef gpio = {0};
  uint8_t time_bcd[7];

  __HAL_RCC_GPIOB_CLK_ENABLE();
  gpio.Pin = SD2058_SCL_PIN | SD2058_SDA_PIN;
  gpio.Mode = GPIO_MODE_OUTPUT_OD;
  gpio.Speed = GPIO_SPEED_FREQ_HIGH;
  HAL_GPIO_Init(SD2058_PORT, &gpio);
  Sd2058_Sda(1U);
  Sd2058_Scl(1U);
  return Sd2058_ReadTime(time_bcd);
}

/**
 * @brief 读取SD2058从秒到年的7字节BCD时间。
 *
 * 简易流程图：
 *   START → 写器件写地址0x64 → 写起始寄存器0x00
 *       → 重复START → 写器件读地址0x65
 *       → 读7字节（前6字节ACK，最后1字节NACK）
 *       → STOP
 */
uint8_t Sd2058_ReadTime(uint8_t time_bcd[7])
{
  uint8_t index;

  if (time_bcd == 0)
  {
    return 0U;
  }
  if ((Sd2058_Start() == 0U) ||
      (Sd2058_WriteByte(SD2058_WRITE_ADDR) == 0U) ||
      (Sd2058_WriteByte(0x00U) == 0U) ||
      (Sd2058_Start() == 0U) ||
      (Sd2058_WriteByte(SD2058_READ_ADDR) == 0U))
  {
    Sd2058_Stop();
    return 0U;
  }
  for (index = 0U; index < 7U; index++)
  {
    time_bcd[index] = Sd2058_ReadByte(index < 6U);
  }
  Sd2058_Stop();
  return 1U;
}

static uint8_t Sd2058_ReadRegisters(uint8_t address, uint8_t *data,
                                    uint8_t length)
{
  uint8_t index;
  if ((data == 0) || (length == 0U) ||
      (Sd2058_Start() == 0U) ||
      (Sd2058_WriteByte(SD2058_WRITE_ADDR) == 0U) ||
      (Sd2058_WriteByte((uint8_t)(address & 0x3FU)) == 0U) ||
      (Sd2058_Start() == 0U) ||
      (Sd2058_WriteByte(SD2058_READ_ADDR) == 0U))
  {
    Sd2058_Stop();
    return 0U;
  }
  for (index = 0U; index < length; ++index)
  {
    data[index] = Sd2058_ReadByte(index < (uint8_t)(length - 1U));
  }
  Sd2058_Stop();
  return 1U;
}

static uint8_t Sd2058_WriteRegisters(uint8_t address, const uint8_t *data,
                                     uint8_t length)
{
  uint8_t index;
  if ((data == 0) || (length == 0U) ||
      (Sd2058_Start() == 0U) ||
      (Sd2058_WriteByte(SD2058_WRITE_ADDR) == 0U) ||
      (Sd2058_WriteByte((uint8_t)(address & 0x3FU)) == 0U))
  {
    Sd2058_Stop();
    return 0U;
  }
  for (index = 0U; index < length; ++index)
  {
    if (Sd2058_WriteByte(data[index]) == 0U)
    {
      Sd2058_Stop();
      return 0U;
    }
  }
  Sd2058_Stop();
  return 1U;
}

static uint8_t Sd2058_ValidBcd(uint8_t value)
{
  return (uint8_t)(((value & 0x0FU) <= 9U) &&
                   (((value >> 4) & 0x0FU) <= 9U));
}

/**
 * @brief 校验并写入PC通过0x02命令下发的7字节BCD时间。
 *
 * 简易流程图：
 *   校验7字节每个半字节都是0～9
 *       → 将PC顺序反转为RTC寄存器顺序
 *       → 读取控制寄存器0x0F/0x10
 *       → 打开RTC写保护窗口
 *       → 写7字节时间
 *       → 恢复控制寄存器并退出写状态
 */
uint8_t Sd2058_WriteTime(const uint8_t pc_time_bcd[7])
{
  uint8_t rtc_time[7];
  uint8_t control1;
  uint8_t control2;
  uint8_t index;
  if (pc_time_bcd == 0)
  {
    return 0U;
  }
  for (index = 0U; index < 7U; ++index)
  {
    if (Sd2058_ValidBcd(pc_time_bcd[index]) == 0U)
    {
      return 0U;
    }
  }
  rtc_time[0] = pc_time_bcd[6];
  rtc_time[1] = pc_time_bcd[5];
  rtc_time[2] = (uint8_t)(pc_time_bcd[4] | 0x80U);
  rtc_time[3] = pc_time_bcd[3];
  rtc_time[4] = pc_time_bcd[2];
  rtc_time[5] = pc_time_bcd[1];
  rtc_time[6] = pc_time_bcd[0];
  if ((Sd2058_ReadRegisters(0x0FU, &control1, 1U) == 0U) ||
      (Sd2058_ReadRegisters(0x10U, &control2, 1U) == 0U))
  {
    return 0U;
  }
  control1 |= 0x84U;
  control2 |= 0x80U;
  if ((Sd2058_WriteRegisters(0x10U, &control2, 1U) == 0U) ||
      (Sd2058_WriteRegisters(0x0FU, &control1, 1U) == 0U) ||
      (Sd2058_WriteRegisters(0x00U, rtc_time, 7U) == 0U))
  {
    return 0U;
  }
  control1 &= 0x7BU;
  control2 &= 0x7FU;
  return (uint8_t)(Sd2058_WriteRegisters(0x0FU, &control1, 1U) &&
                   Sd2058_WriteRegisters(0x10U, &control2, 1U));
}
