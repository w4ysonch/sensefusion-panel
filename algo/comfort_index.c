#include <string.h>
#include "comfort_index.h"
#include "../app/app_init.h"
#include "../app/app_events.h"

/* Steadman 体感温度（热指数）近似公式。
 * 有效范围：温度 20~50°C，湿度 40~100%。超出范围直接跳过，不套公式。 */
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

static comfort_level_t classify(float hi)
{
    if (hi < 10.0f) return COMFORT_COLD;
    if (hi < 20.0f) return COMFORT_COOL;
    if (hi < 28.0f) return COMFORT_COMFORTABLE;
    if (hi < 35.0f) return COMFORT_WARM;
    return COMFORT_HOT;
}

void algo_comfort_on_dht11(const void *payload, size_t size, void *ctx)
{
    (void)ctx;
    if (size < sizeof(evt_dht11_t)) return;

    const evt_dht11_t *raw = (const evt_dht11_t *)payload;

    /* Steadman 公式仅在有效范围内可信；超出范围用温度直接分级。 */
    float hi;
    if (raw->temperature >= 20.0f && raw->humidity >= 40.0f) {
        hi = heat_index(raw->temperature, raw->humidity);
    } else {
        hi = raw->temperature;
    }

    evt_comfort_t ev;
    ev.heat_index = hi;
    ev.level      = (uint8_t)classify(hi);

    embedmq_post(g_mq, EVT_ALGO_COMFORT, &ev, sizeof(ev));
}
