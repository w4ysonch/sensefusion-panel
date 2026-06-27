#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include "sensor_light.h"
#include "../app/app_init.h"
#include "../app/app_events.h"

#ifdef SIMULATOR

static unsigned int g_seed = 0xDEAD5555u;
static float s_lux = 300.0f;

static int read_light(uint16_t *lux)
{
    float r = (float)rand_r(&g_seed) / (float)RAND_MAX;
    float delta = (r * 2.0f - 1.0f) * 20.0f;
    s_lux += delta;
    if (s_lux < 10.0f)   s_lux = 10.0f;
    if (s_lux > 800.0f)  s_lux = 800.0f;
    *lux = (uint16_t)s_lux;
    return 0;
}

#else

#include <fcntl.h>
#include <sys/ioctl.h>
#include <linux/i2c-dev.h>
#include <linux/i2c.h>

#define AP3216C_ADDR  0x1e
#define AP3216C_I2C   "/dev/i2c-0"

static int read_light(uint16_t *lux)
{
    static int fd = -1;

    /* 首次调用：打开 I2C 并初始化 AP3216C */
    if (fd < 0) {
        fd = open(AP3216C_I2C, O_RDWR);
        if (fd < 0) {
            perror("[light] open i2c");
            return -1;
        }
        ioctl(fd, I2C_SLAVE, AP3216C_ADDR);

        /* 复位 + 启用 ALS（System Configuration register 0x00） */
        unsigned char cfg[2];
        struct i2c_msg  msgs[1];
        struct i2c_rdwr_ioctl_data rdwr;
        int ret;

        cfg[0] = 0x00;  cfg[1] = 0x04;  /* reset */
        msgs[0].addr = AP3216C_ADDR; msgs[0].flags = 0;
        msgs[0].len = 2; msgs[0].buf = cfg;
        rdwr.msgs = msgs; rdwr.nmsgs = 1;
        ret = ioctl(fd, I2C_RDWR, &rdwr);
        if (ret < 0) { perror("[light] reset"); close(fd); fd = -1; return -1; }

        cfg[0] = 0x00;  cfg[1] = 0x03;  /* enable ALS + PS */
        ret = ioctl(fd, I2C_RDWR, &rdwr);
        if (ret < 0) { perror("[light] enable"); close(fd); fd = -1; return -1; }

        printf("[light] AP3216C 初始化完成\n");
    }

    /* 读 ALS 数据寄存器 0x0C~0x0D */
    unsigned char tx = 0x0C;
    unsigned char rx[2];
    struct i2c_msg msgs[2] = {
        { .addr = AP3216C_ADDR, .flags = 0,        .len = 1, .buf = &tx },
        { .addr = AP3216C_ADDR, .flags = I2C_M_RD,  .len = 2, .buf = rx  },
    };
    struct i2c_rdwr_ioctl_data rdwr = { .msgs = msgs, .nmsgs = 2 };

    if (ioctl(fd, I2C_RDWR, &rdwr) != 2) {
        perror("[light] read");
        return -1;
    }

    *lux = ((uint16_t)rx[1] << 8) | rx[0];
    return 0;
}

#endif /* SIMULATOR */

void *sensor_light_thread(void *arg)
{
    (void)arg;
    uint32_t uuid = embedmq_uuid(EVT_SENSOR_LIGHT);

    while (1) {
        evt_light_t ev;
        if (read_light(&ev.lux) == 0)
            embedmq_post_id(g_mq, uuid, &ev, sizeof(ev));

        sleep(1);
    }
    return NULL;
}
