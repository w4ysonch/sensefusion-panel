#include "ui_handlers.h"
#include "ui_dashboard.hpp"
#include "../common/app_common.h"
#include "../algo/comfort_index.hpp"

extern "C" void ui_on_dht11(const void *payload, size_t size, void *ctx)
{
    (void)ctx;
    if (size < sizeof(evt_dht11_t)) return;
    const auto *ev = static_cast<const evt_dht11_t *>(payload);
    Dashboard::instance().update_dht11(ev->temperature, ev->humidity);
}

extern "C" void ui_on_adxl345(const void *payload, size_t size, void *ctx)
{
    (void)ctx;
    if (size < sizeof(evt_adxl345_t)) return;
    const auto *ev = static_cast<const evt_adxl345_t *>(payload);
    Dashboard::instance().update_accel(ev->x, ev->y, ev->z, ev->magnitude);
}

extern "C" void ui_on_sr501(const void *payload, size_t size, void *ctx)
{
    (void)ctx;
    if (size < sizeof(evt_sr501_t)) return;
    const auto *ev = static_cast<const evt_sr501_t *>(payload);
    Dashboard::instance().update_pir(ev->detected);
}

extern "C" void ui_on_sr04(const void *payload, size_t size, void *ctx)
{
    (void)ctx;
    if (size < sizeof(evt_sr04_t)) return;
    const auto *ev = static_cast<const evt_sr04_t *>(payload);
    Dashboard::instance().update_distance(ev->distance_cm);
}

extern "C" void ui_on_light(const void *payload, size_t size, void *ctx)
{
    (void)ctx;
    if (size < sizeof(evt_light_t)) return;
    const auto *ev = static_cast<const evt_light_t *>(payload);
    Dashboard::instance().update_light(ev->lux);
}

extern "C" void ui_on_comfort(const void *payload, size_t size, void *ctx)
{
    (void)ctx;
    if (size < sizeof(evt_comfort_t)) return;
    const auto *ev = static_cast<const evt_comfort_t *>(payload);
    Dashboard::instance().update_comfort(ev->heat_index,
                                         static_cast<ComfortLevel>(ev->level));
}

extern "C" void ui_on_anomaly(const void *payload, size_t size, void *ctx)
{
    (void)ctx;
    if (size < sizeof(evt_anomaly_t)) return;
    const auto *ev = static_cast<const evt_anomaly_t *>(payload);
    Dashboard::instance().show_alert(ev->magnitude);
}

extern "C" void ui_on_touch(const void *payload, size_t size, void *ctx)
{
    (void)ctx;
    if (size < sizeof(evt_touch_t)) return;
    const auto *ev = static_cast<const evt_touch_t *>(payload);
    Dashboard::instance().update_touch(ev->x, ev->y, ev->pressed);
}

extern "C" void ui_on_ir(const void *payload, size_t size, void *ctx)
{
    (void)ctx;
    if (size < sizeof(evt_ir_t)) return;
    const auto *ev = static_cast<const evt_ir_t *>(payload);
    Dashboard::instance().handle_ir_key(ev->key_code);
}
