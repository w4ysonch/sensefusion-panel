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

/* 通过 ADC sysfs 读取光敏电阻分压，换算为近似 lux */
static int read_light(uint16_t *lux)
{
    /* TODO: 读 /sys/bus/iio/devices/iio:deviceX/in_voltage_raw，
     * 再用校准系数换算为 lux */
    *lux = 320;
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
