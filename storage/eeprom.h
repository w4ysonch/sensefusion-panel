#ifndef EEPROM_H
#define EEPROM_H

#include <stdint.h>
#include <stddef.h>


#ifdef __cplusplus
extern "C" {
#endif

int  eeprom_write(uint16_t addr, const void *data, size_t len);
int  eeprom_read (uint16_t addr, void *data, size_t len);


#ifdef __cplusplus
}
#endif

#endif /* EEPROM_H */
