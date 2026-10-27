#ifndef SD2058_H
#define SD2058_H

#include <stdint.h>

/** Initialize the legacy SD2058 software-I2C bus on PB6/PB7. */
uint8_t Sd2058_Init(void);

/** Read registers 0x00..0x06 (second through year, BCD encoded). */
uint8_t Sd2058_ReadTime(uint8_t time_bcd[7]);

/** Write year/month/day/week/hour/minute/second BCD values from command 0x02. */
uint8_t Sd2058_WriteTime(const uint8_t pc_time_bcd[7]);

#endif /* SD2058_H */
