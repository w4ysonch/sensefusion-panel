#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/i2c-dev.h>
#include "eeprom.h"

#define EEPROM_I2C_BUS  "/dev/i2c-0"
#define EEPROM_I2C_ADDR 0x50

int eeprom_write(uint16_t addr, const void *data, size_t len)
{
    /* TODO: open EEPROM_I2C_BUS，ioctl I2C_SLAVE EEPROM_I2C_ADDR，
     * 发送 [addr_hi, addr_lo, data...]，再等待 5ms 写周期完成 */
    (void)addr; (void)data; (void)len;
    return 0;
}

int eeprom_read(uint16_t addr, void *data, size_t len)
{
    /* TODO: 先写 2 字节地址，再读 len 字节数据 */
    (void)addr; (void)data; (void)len;
    return 0;
}
