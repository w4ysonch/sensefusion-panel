#include <cstdio>
#include <cstdlib>
#include <unistd.h>
#include "sensor_sr501.h"
#include "../common/app_common.h"

#include "../common/g_running.hpp"

#ifdef SIMULATOR

static int     s_counter = 0;
static uint8_t s_state   = 0;

static int read_sr501(uint8_t *detected)
{
    if (++s_counter >= 60) {
        s_counter = 0;
        s_state ^= 1u;
    }
    *detected = s_state;
    return 0;
}

#else

static int read_sr501(uint8_t *detected)
{
    *detected = 0;
    return 0;
}

#endif /* SIMULATOR */

void *sensor_sr501_thread(void *arg)
{
    (void)arg;
    uint32_t uuid    = embedmq_uuid(EVT_SENSOR_SR501);
    uint8_t last_val = 0xFF;

    while (g_running.load()) {
        evt_sr501_t ev;
        if (read_sr501(&ev.detected) == 0 && ev.detected != last_val) {
            last_val = ev.detected;
            embedmq_post_id(g_mq, uuid, &ev, sizeof(ev));
        }
        usleep(100000);
    }
    return nullptr;
}
