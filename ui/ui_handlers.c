#include <string.h>
#include "ui_handlers.h"
#include "ui_dashboard.h"
#include "../common/app_common.h"
#include "../algo/comfort_index.h"

void ui_on_dht11(const void *payload, size_t size, void *ctx)
{
    (void)ctx;
    if (size < sizeof(evt_dht11_t)) return;
    const evt_dht11_t *ev = (const evt_dht11_t *)payload;
    dashboard_update_dht11(ev->temperature, ev->humidity);
}

void ui_on_adxl345(const void *payload, size_t size, void *ctx)
{
    (void)ctx;
    if (size < sizeof(evt_adxl345_t)) return;
    const evt_adxl345_t *ev = (const evt_adxl345_t *)payload;
    dashboard_update_accel(ev->x, ev->y, ev->z, ev->magnitude);
}

void ui_on_sr501(const void *payload, size_t size, void *ctx)
{
    (void)ctx;
    if (size < sizeof(evt_sr501_t)) return;
    const evt_sr501_t *ev = (const evt_sr501_t *)payload;
    dashboard_update_pir(ev->detected);
}

void ui_on_sr04(const void *payload, size_t size, void *ctx)
{
    (void)ctx;
    if (size < sizeof(evt_sr04_t)) return;
    const evt_sr04_t *ev = (const evt_sr04_t *)payload;
    dashboard_update_distance(ev->distance_cm);
}

void ui_on_light(const void *payload, size_t size, void *ctx)
{
    (void)ctx;
    if (size < sizeof(evt_light_t)) return;
    const evt_light_t *ev = (const evt_light_t *)payload;
    dashboard_update_light(ev->lux);
}

void ui_on_comfort(const void *payload, size_t size, void *ctx)
{
    (void)ctx;
    if (size < sizeof(evt_comfort_t)) return;
    const evt_comfort_t *ev = (const evt_comfort_t *)payload;
    dashboard_update_comfort(ev->heat_index, (comfort_level_t)ev->level);
}

void ui_on_anomaly(const void *payload, size_t size, void *ctx)
{
    (void)ctx;
    if (size < sizeof(evt_anomaly_t)) return;
    const evt_anomaly_t *ev = (const evt_anomaly_t *)payload;
    dashboard_show_alert(ev->magnitude);
}

void ui_on_touch(const void *payload, size_t size, void *ctx)
{
    (void)ctx;
    if (size < sizeof(evt_touch_t)) return;
    const evt_touch_t *ev = (const evt_touch_t *)payload;
    dashboard_update_touch(ev->x, ev->y, ev->pressed);
}

void ui_on_ir(const void *payload, size_t size, void *ctx)
{
    (void)ctx;
    if (size < sizeof(evt_ir_t)) return;
    const evt_ir_t *ev = (const evt_ir_t *)payload;
    dashboard_handle_ir_key(ev->key_code);
}
