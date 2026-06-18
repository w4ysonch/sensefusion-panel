#include <stdio.h>
#include <unistd.h>
#include "sensor_dht11.h"
#include "../app/app_init.h"
#include "../app/app_events.h"

/* TODO: DHT11 单总线协议需要内核驱动支持，驱动开发完成后再实现
 * 读取方式待定（字符设备 or sysfs），接口确认后填充此函数 */
static int read_dht11(float *temp, float *humidity)
{
    *temp     = 26.0f;   /* 占位 */
    *humidity = 65.0f;
    return 0;
}

void *sensor_dht11_thread(void *arg)
{
    (void)arg;
    /* 提前缓存 UUID，避免循环内每次都做字符串哈希 */
    uint32_t uuid = embedmq_uuid(EVT_SENSOR_DHT11);

    while (1) {
        evt_dht11_t ev;
        if (read_dht11(&ev.temperature, &ev.humidity) == 0)
            embedmq_post_id(g_mq, uuid, &ev, sizeof(ev));

        sleep(2);  /* DHT11 最快采样间隔 1s，保守取 2s */
    }
    return NULL;
}
