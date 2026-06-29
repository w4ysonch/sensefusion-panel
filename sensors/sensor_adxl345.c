#include <stdio.h>
#include <unistd.h>
#include <math.h>
#include <stdlib.h>
#include "sensor_adxl345.h"
#include "../app/app_init.h"
#include "../app/app_events.h"

#ifdef SIMULATOR

#include "../sim/sim_utils.h"

static unsigned int g_seed = 0xDEAD2222u;
static float s_ax = 0.0f;
static float s_ay = 0.0f;
static float s_az = 1.0f;

static int read_adxl345(float *x, float *y, float *z)
{
    /* 0.5% 概率触发冲击峰值（约每 20s 一次），用于测试告警横幅 */
    float spike = (float)rand_r(&g_seed) / (float)RAND_MAX;
    if (spike < 0.005f) {
        float dir = (float)rand_r(&g_seed) / (float)RAND_MAX * 2.0f - 1.0f;
        *x = dir * 2.5f;
        *y = ((float)rand_r(&g_seed) / (float)RAND_MAX * 2.0f - 1.0f) * 2.5f;
        *z = 1.0f;
    } else {
        s_ax = sim_walk(s_ax, -0.3f,  0.3f, 0.05f, &g_seed);
        s_ay = sim_walk(s_ay, -0.3f,  0.3f, 0.05f, &g_seed);
        s_az = sim_walk(s_az,  0.85f, 1.15f, 0.02f, &g_seed);
        *x = s_ax;
        *y = s_ay;
        *z = s_az;
    }
    return 0;
}

#else

/* TODO: ADXL345 通过 I2C 读取，I2C 总线号和设备地址需上板确认
 * 参考：I2C 地址 0x53（SDO=GND）或 0x1D（SDO=VCC），寄存器 0x32~0x37 */
static int read_adxl345(float *x, float *y, float *z)
{
    *x = 0.01f;
    *y = 0.02f;
    *z = 1.00f;
    return 0;
}

#endif /* SIMULATOR */

void *sensor_adxl345_thread(void *arg)
{
    (void)arg;
    uint32_t uuid = embedmq_uuid(EVT_SENSOR_ADXL345);

    while (1) {
        evt_adxl345_t ev;
        if (read_adxl345(&ev.x, &ev.y, &ev.z) == 0) {
            ev.magnitude = sqrtf(ev.x*ev.x + ev.y*ev.y + ev.z*ev.z);
            embedmq_post_id(g_mq, uuid, &ev, sizeof(ev));
        }
        usleep(100000);  /* 100ms，采样率 10Hz */
    }
    return NULL;
}
