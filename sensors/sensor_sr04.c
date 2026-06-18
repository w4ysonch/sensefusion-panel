#include <stdio.h>
#include <unistd.h>
#include "sensor_sr04.h"
#include "../app/app_init.h"
#include "../app/app_events.h"

/* SR04 超声波测距：GPIO 产生 10µs Trig 脉冲，测量 Echo 脉冲宽度换算距离 */
static int read_sr04(float *distance_cm)
{
    /* TODO: Trig GPIO 拉高 10µs → 等待 Echo 上升沿 → 计时 → 下降沿
     * distance = echo_us / 58.0（声速 340m/s 推导）*/
    *distance_cm = 80.0f;   /* 占位 */
    return 0;
}

void *sensor_sr04_thread(void *arg)
{
    (void)arg;
    uint32_t uuid = embedmq_uuid(EVT_SENSOR_SR04);

    while (1) {
        evt_sr04_t ev;
        if (read_sr04(&ev.distance_cm) == 0)
            embedmq_post_id(g_mq, uuid, &ev, sizeof(ev));

        usleep(500000);  /* 500ms，避免超声波互扰 */
    }
    return NULL;
}
