#ifndef UI_HANDLERS_H
#define UI_HANDLERS_H

#include <stddef.h>

/* embedmq handler 回调，在 app_init.c 中注册 */
void ui_on_dht11   (const void *payload, size_t size, void *ctx);
void ui_on_adxl345 (const void *payload, size_t size, void *ctx);
void ui_on_sr501   (const void *payload, size_t size, void *ctx);
void ui_on_sr04    (const void *payload, size_t size, void *ctx);
void ui_on_light   (const void *payload, size_t size, void *ctx);
void ui_on_comfort (const void *payload, size_t size, void *ctx);
void ui_on_anomaly (const void *payload, size_t size, void *ctx);
void ui_on_ir      (const void *payload, size_t size, void *ctx);

#endif /* UI_HANDLERS_H */
