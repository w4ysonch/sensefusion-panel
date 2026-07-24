#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <unistd.h>
#include "sensor_adxl345.h"
#include "../common/app_common.h"

#include "../common/g_running.hpp"

#ifdef SIMULATOR
#include "../sim/sim_utils.h"

static unsigned int g_seed = 0xDEAD2222u;
static float s_ax = 0.0f;
static float s_ay = 0.0f;
static float s_az = 1.0f;

static int read_adxl345(float *x, float *y, float *z)
{
    float spike = static_cast<float>(rand_r(&g_seed)) / static_cast<float>(RAND_MAX);
    if (spike < 0.005f) {
        float dir = static_cast<float>(rand_r(&g_seed)) / static_cast<float>(RAND_MAX) * 2.0f - 1.0f;
        *x = dir * 2.5f;
        *y = (static_cast<float>(rand_r(&g_seed)) / static_cast<float>(RAND_MAX) * 2.0f - 1.0f) * 2.5f;
        *z = 1.0f;
    } else {
        s_ax = sim_walk(s_ax, -0.3f,  0.3f,  0.05f, &g_seed);
        s_ay = sim_walk(s_ay, -0.3f,  0.3f,  0.05f, &g_seed);
        s_az = sim_walk(s_az,  0.85f, 1.15f, 0.02f, &g_seed);
        *x = s_ax;
        *y = s_ay;
        *z = s_az;
    }
    return 0;
}

#else

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

    while (g_running.load()) {
        evt_adxl345_t ev;
        if (read_adxl345(&ev.x, &ev.y, &ev.z) == 0) {
            ev.magnitude = sqrtf(ev.x*ev.x + ev.y*ev.y + ev.z*ev.z);
            embedmq_post_id(g_mq, uuid, &ev, sizeof(ev));
        }
        usleep(100000);
    }
    return nullptr;
}
