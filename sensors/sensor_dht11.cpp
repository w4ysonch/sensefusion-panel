#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <unistd.h>
#include "sensor_dht11.h"
#include "../common/app_common.h"

#include "../common/g_running.hpp"

#ifdef SIMULATOR
#include "../sim/sim_utils.h"

static unsigned int g_seed = 0xDEAD1111u;
static float s_temp     = 25.0f;
static float s_humidity = 60.0f;

static int read_dht11(float *temp, float *humidity)
{
    s_temp     = sim_walk(s_temp,     18.0f, 40.0f, 0.3f, &g_seed);
    s_humidity = sim_walk(s_humidity, 30.0f, 90.0f, 1.0f, &g_seed);
    *temp     = s_temp;
    *humidity = s_humidity;
    return 0;
}

#else

static int read_dht11(float *temp, float *humidity)
{
    *temp     = 26.0f;
    *humidity = 65.0f;
    return 0;
}

#endif /* SIMULATOR */

void *sensor_dht11_thread(void *arg)
{
    (void)arg;
    uint32_t uuid = embedmq_uuid(EVT_SENSOR_DHT11);

    while (g_running.load()) {
        evt_dht11_t ev;
        if (read_dht11(&ev.temperature, &ev.humidity) == 0)
            embedmq_post_id(g_mq, uuid, &ev, sizeof(ev));
        sleep(2);
    }
    return nullptr;
}
