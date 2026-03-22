/*
* drivers/pmic_axp15060.h — AXP15060 PMIC Driver Header for JH7110
 */

#ifndef JH7110_PMIC_AXP15060_H
#define JH7110_PMIC_AXP15060_H

#include "types.h"

int   axp15060_init(void);
bool  axp15060_is_available(void);
uint8_t axp15060_get_chip_id(void);
int   axp15060_get_temperature(int32_t *temp_mc);
int   axp15060_read_dcdc_voltage(uint8_t dcdc_num, uint32_t *mv_out);
void  axp15060_print_status(void);

#endif /* JH7110_PMIC_AXP15060_H */