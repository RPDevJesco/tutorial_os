/*
* drivers/i2c.h — I2C Driver Header for JH7110
 */

#ifndef JH7110_I2C_H
#define JH7110_I2C_H

#include "types.h"

void jh7110_i2c_init(uintptr_t base, uint8_t addr_7bit);
bool jh7110_i2c_write_reg(uintptr_t base, uint8_t reg, uint8_t data);
bool jh7110_i2c_read_reg(uintptr_t base, uint8_t reg, uint8_t *data);
bool jh7110_i2c_read_regs(uintptr_t base, uint8_t reg, uint8_t *buf, uint32_t count);

#endif /* JH7110_I2C_H */