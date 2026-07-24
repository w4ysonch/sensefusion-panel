#include <cstdio>
#include <cstdlib>
#include <unistd.h>
#include "sensor_sr04.h"
#include "../common/app_common.h"

#include "../common/g_running.hpp"

#ifdef SIMULATOR

static unsigned int g_seed = 0xDEAD3333u;
static float s_dist = 80.0f;

static int read_sr04(float *distance_cm)
{
    float r = static_cast<float>(rand_r(&g_seed)) / static_cast<float>(RAND_MAX);
    float delta = (r * 2.0f - 1.0f) * 3.0f;
    s_dist += delta;
    if (s_dist < 5.0f)   s_dist = 5.0f;
    if (s_dist > 300.0f) s_dist = 300.0f;
    *distance_cm = s_dist;
    return 0;
}

#else

static int read_sr04(float *distance_cm)
{
    *distance_cm = 80.0f;
    return 0;
}

#endif /* SIMULATOR */

void *sensor_sr04_thread(void *arg)
{
    (void)arg;
    uint32_t uuid = embedmq_uuid(EVT_SENSOR_SR04);

    while (g_running.load()) {
        evt_sr04_t ev;
        if (read_sr04(&ev.distance_cm) == 0)
            embedmq_post_id(g_mq, uuid, &ev, sizeof(ev));
        usleep(500000);
    }
    return nullptr;
}
