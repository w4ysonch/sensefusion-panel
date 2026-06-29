#ifndef UI_DASHBOARD_H
#define UI_DASHBOARD_H

#include <stdint.h>
#include "../algo/comfort_index.h"
#include "../storage/settings.h"

/* 启动时调用一次，初始化 LVGL 及所有控件 */
void dashboard_init(const app_settings_t *settings);

/* 各传感器数据更新接口，可从 embedmq handler 线程安全调用 */
void dashboard_update_dht11   (float temp, float humidity);
void dashboard_update_accel   (float x, float y, float z, float magnitude);
void dashboard_update_pir     (uint8_t detected);
void dashboard_update_distance(float cm);
void dashboard_update_light   (uint16_t lux);
void dashboard_update_comfort (float heat_index, comfort_level_t level);
void dashboard_show_alert     (float magnitude);
void dashboard_update_touch   (int32_t x, int32_t y, uint8_t pressed);
void dashboard_handle_ir_key  (uint16_t key_code);

/* LVGL tick，在主循环中周期调用。
 * 返回值：距下次需要处理的最短等待时间（ms），可直接传给 usleep()。 */
uint32_t dashboard_tick(void);

#endif /* UI_DASHBOARD_H */
