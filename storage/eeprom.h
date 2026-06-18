#ifndef EEPROM_H
#define EEPROM_H

#include <stdint.h>
#include <stddef.h>

int  eeprom_write(uint16_t addr, const void *data, size_t len);
int  eeprom_read (uint16_t addr, void *data, size_t len);

#endif /* EEPROM_H */
