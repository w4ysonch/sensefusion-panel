#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/i2c.h>
#include <linux/i2c-dev.h>
#include "eeprom.h"

/* AT24Cxx 系列 EEPROM：16 位地址，I2C 总线和设备地址待上板确认后修改 */
#define EEPROM_I2C_BUS  "/dev/i2c-0"
#define EEPROM_I2C_ADDR 0x50
#define EEPROM_PAGE_SIZE 8   /* AT24C02 页大小，写操作不能跨页 */
#define EEPROM_WRITE_DELAY_US 5000  /* 写周期等待 5ms */

static int s_fd = -1;

static int eeprom_open(void)
{
    if (s_fd >= 0) return 0;
    s_fd = open(EEPROM_I2C_BUS, O_RDWR);
    if (s_fd < 0) {
        perror("[eeprom] open");
        return -1;
    }
    if (ioctl(s_fd, I2C_SLAVE, EEPROM_I2C_ADDR) < 0) {
        perror("[eeprom] I2C_SLAVE");
        close(s_fd);
        s_fd = -1;
        return -1;
    }
    return 0;
}

int eeprom_write(uint16_t addr, const void *data, size_t len)
{
    if (eeprom_open() < 0) return -1;

    const uint8_t *src = (const uint8_t *)data;
    size_t written = 0;

    while (written < len) {
        /* 不能跨页写：每次写到当前页末尾 */
        uint16_t cur_addr = addr + (uint16_t)written;
        size_t page_rem = EEPROM_PAGE_SIZE - (cur_addr % EEPROM_PAGE_SIZE);
        size_t chunk = len - written;
        if (chunk > page_rem) chunk = page_rem;

        /* 组装写缓冲：[addr_hi, addr_lo, data...] */
        uint8_t buf[2 + EEPROM_PAGE_SIZE];
        buf[0] = (uint8_t)(cur_addr >> 8);
        buf[1] = (uint8_t)(cur_addr & 0xff);
        memcpy(buf + 2, src + written, chunk);

        if (write(s_fd, buf, 2 + chunk) != (ssize_t)(2 + chunk)) {
            perror("[eeprom] write");
            close(s_fd); s_fd = -1;
            return -1;
        }
        usleep(EEPROM_WRITE_DELAY_US);
        written += chunk;
    }
    return 0;
}

int eeprom_read(uint16_t addr, void *data, size_t len)
{
    if (eeprom_open() < 0) return -1;

    /* 先写 2 字节地址，再 repeated start 读数据 */
    uint8_t addr_buf[2] = { (uint8_t)(addr >> 8), (uint8_t)(addr & 0xff) };
    struct i2c_msg msgs[2] = {
        { .addr = EEPROM_I2C_ADDR, .flags = 0,       .len = 2,   .buf = addr_buf },
        { .addr = EEPROM_I2C_ADDR, .flags = I2C_M_RD, .len = (uint16_t)len, .buf = (uint8_t *)data },
    };
    struct i2c_rdwr_ioctl_data rdwr = { .msgs = msgs, .nmsgs = 2 };

    if (ioctl(s_fd, I2C_RDWR, &rdwr) != 2) {
        perror("[eeprom] read");
        close(s_fd); s_fd = -1;
        return -1;
    }
    return 0;
}
