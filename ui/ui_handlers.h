#ifndef UI_HANDLERS_H
#define UI_HANDLERS_H

#include <stddef.h>

/* embedmq 回调，注册到 ui_app 的 g_mq。
 * 只负责更新 sensor_cache_t（通过 dashboard_update_*），
 * 不做 algo / db / mqtt——那些在 sensor_daemon 侧已完成。 */
void ui_on_dht11   (const void *payload, size_t size, void *ctx);
void ui_on_adxl345 (const void *payload, size_t size, void *ctx);
void ui_on_sr501   (const void *payload, size_t size, void *ctx);
void ui_on_sr04    (const void *payload, size_t size, void *ctx);
void ui_on_light   (const void *payload, size_t size, void *ctx);
void ui_on_comfort (const void *payload, size_t size, void *ctx);
void ui_on_anomaly (const void *payload, size_t size, void *ctx);
void ui_on_touch   (const void *payload, size_t size, void *ctx);
void ui_on_ir      (const void *payload, size_t size, void *ctx);

#endif /* UI_HANDLERS_H */
