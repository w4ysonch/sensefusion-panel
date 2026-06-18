#include <stdio.h>
#include <string.h>
#include "ui_handlers.h"
#include "ui_dashboard.h"
#include "../app/app_events.h"
#include "../algo/comfort_index.h"
#include "../algo/anomaly.h"

/* 以下 8 个函数是 embedmq handler 回调，运行在 embedmq 消费者线程上。
 * embedmq 每个事件只能绑一个 handler，所以传感器数据的显示和算法处理
 * 都在这里串联调用，不需要在 app_init 里重复注册。 */

void ui_on_dht11(const void *payload, size_t size, void *ctx)
{
    (void)ctx;
    if (size < sizeof(evt_dht11_t)) return;
    const evt_dht11_t *ev = (const evt_dht11_t *)payload;
    dashboard_update_dht11(ev->temperature, ev->humidity);
    algo_comfort_on_dht11(payload, size, NULL);  /* 顺带触发舒适度计算 */
}

void ui_on_adxl345(const void *payload, size_t size, void *ctx)
{
    (void)ctx;
    if (size < sizeof(evt_adxl345_t)) return;
    const evt_adxl345_t *ev = (const evt_adxl345_t *)payload;
    dashboard_update_accel(ev->x, ev->y, ev->z, ev->magnitude);
    algo_anomaly_on_adxl345(payload, size, NULL);  /* 顺带触发异常检测 */
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
    dashboard_show_alert(ev->type, ev->magnitude);
}

void ui_on_ir(const void *payload, size_t size, void *ctx)
{
    (void)ctx;
    if (size < sizeof(evt_ir_t)) return;
    const evt_ir_t *ev = (const evt_ir_t *)payload;
    dashboard_handle_ir_key(ev->key_code);
}
