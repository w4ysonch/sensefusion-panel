#include "comfort_index.hpp"
#include "../common/app_common.h"

#define HI_VALID_TEMP_MIN  20.0f
#define HI_VALID_HUMI_MIN  40.0f

#define HI_THRESH_COLD         10.0f
#define HI_THRESH_COOL         20.0f
#define HI_THRESH_COMFORTABLE  28.0f
#define HI_THRESH_WARM         35.0f

static float heat_index(float t, float rh)
{
    return -8.78469475556f
        + 1.61139411f    * t
        + 2.33854883889f * rh
        - 0.14611605f    * t  * rh
        - 0.012308094f   * t  * t
        - 0.016424828f   * rh * rh
        + 0.002211732f   * t  * t  * rh
        + 0.00072546f    * t  * rh * rh
        - 0.000003582f   * t  * t  * rh * rh;
}

static ComfortLevel classify(float hi)
{
    if (hi < HI_THRESH_COLD)         return ComfortLevel::Cold;
    if (hi < HI_THRESH_COOL)         return ComfortLevel::Cool;
    if (hi < HI_THRESH_COMFORTABLE)  return ComfortLevel::Comfortable;
    if (hi < HI_THRESH_WARM)         return ComfortLevel::Warm;
    return ComfortLevel::Hot;
}

extern "C" void algo_comfort_on_dht11(const void *payload, size_t size, void *ctx)
{
    (void)ctx;
    if (size < sizeof(evt_dht11_t)) return;

    const auto *raw = static_cast<const evt_dht11_t *>(payload);

    float hi;
    if (raw->temperature >= HI_VALID_TEMP_MIN && raw->humidity >= HI_VALID_HUMI_MIN)
        hi = heat_index(raw->temperature, raw->humidity);
    else
        hi = raw->temperature;

    evt_comfort_t ev{};
    ev.heat_index = hi;
    ev.level      = static_cast<uint8_t>(classify(hi));

    embedmq_post(g_mq, EVT_ALGO_COMFORT, &ev, sizeof(ev));
}
