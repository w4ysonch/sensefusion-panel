#include <stdio.h>
#include <unistd.h>
#include "sensor_light.h"
#include "../app/app_init.h"
#include "../app/app_events.h"

/* 通过 ADC sysfs 读取光敏电阻分压，换算为近似 lux */
static int read_light(uint16_t *lux)
{
    /* TODO: 读 /sys/bus/iio/devices/iio:deviceX/in_voltage_raw，
     * 再用校准系数换算为 lux */
    *lux = 320;   /* 占位 */
    return 0;
}

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
