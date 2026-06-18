#include <stdio.h>
#include <unistd.h>
#include <math.h>
#include "sensor_adxl345.h"
#include "../app/app_init.h"
#include "../app/app_events.h"

/* TODO: ADXL345 通过 I2C 读取，I2C 总线号和设备地址需上板确认
 * 参考：I2C 地址 0x53（SDO=GND）或 0x1D（SDO=VCC），寄存器 0x32~0x37 */
static int read_adxl345(float *x, float *y, float *z)
{
    *x = 0.01f;
    *y = 0.02f;
    *z = 1.00f;  /* 水平静置时 z ≈ 1g */
    return 0;
}

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
