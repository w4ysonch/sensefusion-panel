#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include "sensor_sr501.h"
#include "../app/app_init.h"
#include "../app/app_events.h"

#ifdef SIMULATOR

static int     s_counter = 0;
static uint8_t s_state   = 0;

static int read_sr501(uint8_t *detected)
{
    /* 每 60 次（约 6 秒）确定性翻转，模拟器下行为可预期 */
    if (++s_counter >= 60) {
        s_counter = 0;
        s_state ^= 1u;
    }
    *detected = s_state;
    return 0;
}

#else

/* 读取 SR501 人体红外传感器输出（GPIO 高/低电平） */
static int read_sr501(uint8_t *detected)
{
    /* TODO: 读 /sys/class/gpio/gpioXX/value，返回 0 或 1 */
    *detected = 0;
    return 0;
}

#endif /* SIMULATOR */

void *sensor_sr501_thread(void *arg)
{
    (void)arg;
    uint32_t uuid     = embedmq_uuid(EVT_SENSOR_SR501);
    uint8_t  last_val = 0xFF;  /* 强制首次上报，让 ui_app 连上来就有初始值 */

    while (1) {
        evt_sr501_t ev;
        if (read_sr501(&ev.detected) == 0 && ev.detected != last_val) {
            last_val = ev.detected;
            embedmq_post_id(g_mq, uuid, &ev, sizeof(ev));
        }
        usleep(100000);  /* 每 100ms 轮询一次 */
    }
    return NULL;
}
