#pragma once

#include <cstdint>
#include <pthread.h>
#include "lvgl/lvgl.h"
#include "../algo/comfort_index.hpp"
#include "../storage/settings.h"

/* LvglMemberEventThunk — 把成员函数转成 lv_event_cb_t
 *
 * 用法：
 *   lv_obj_add_event_cb(obj,
 *       LvglMemberEventThunk<Dashboard, &Dashboard::OnFoo>,
 *       LV_EVENT_CLICKED, this);
 *
 * LVGL 回调时传入 user_data=this，thunk 从中还原指针并调用成员函数。
 */
template<typename T, void (T::*Method)(lv_event_t *)>
static void LvglMemberEventThunk(lv_event_t *e)
{
    (static_cast<T *>(lv_event_get_user_data(e))->*Method)(e);
}

/* ── 传感器缓存（线程间共享，mutex 保护） ─────────────────────── */
struct SensorCache {
    bool dht11_dirty   = false;
    bool accel_dirty   = false;
    bool pir_dirty     = false;
    bool dist_dirty    = false;
    bool light_dirty   = false;
    bool comfort_dirty = false;
    bool anomaly_dirty = false;
    bool touch_dirty   = false;
    bool ir_dirty      = false;

    float    temp{}, humidity{};
    float    ax{}, ay{}, az{}, amag{};
    uint8_t  pir_detected{};
    float    dist_cm{};
    uint16_t lux{};
    float    heat_index{};
    uint8_t  comfort_level{};
    float    anomaly_mag{};
    int32_t  touch_x{}, touch_y{};
    uint8_t  touch_pressed{};
    int32_t  ir_key{};
};

/* ── 趋势卡片元数据 ───────────────────────────────────────────── */
struct ChartMeta {
    lv_obj_t          *src_chart{};
    lv_chart_series_t *src_ser{};
    int32_t            y_min{};
    int32_t            y_max{};
    lv_color_t         color{};
    const char        *title{};
};

/* ── Dashboard 单例 ──────────────────────────────────────────── */
class Dashboard {
public:
    static Dashboard &instance();

    void init(const app_settings_t *settings);
    uint32_t tick();

    /* 线程安全写缓存接口（embedmq 消费者线程调用） */
    void update_dht11   (float temp, float humidity);
    void update_accel   (float x, float y, float z, float mag);
    void update_pir     (uint8_t detected);
    void update_distance(float cm);
    void update_light   (uint16_t lux);
    void update_comfort (float heat_index, ComfortLevel level);
    void show_alert     (float magnitude);
    void update_touch   (int32_t x, int32_t y, uint8_t pressed);
    void handle_ir_key  (uint16_t key_code);

private:
    Dashboard() = default;

    void build_ui();
    void build_tab_overview(lv_obj_t *tab);
    void build_tab_trend   (lv_obj_t *tab);
    void build_tab_settings(lv_obj_t *tab);
    void build_tab_system  (lv_obj_t *tab);
    void build_detail_panel(lv_obj_t *scr);
    void refresh_sysinfo();
    void alert_slide_in();
    void alert_slide_out();

    /* LVGL 事件回调（成员函数） */
    void OnDetailClose     (lv_event_t *e);
    void OnChartCardClick  (lv_event_t *e);
    void OnBrightnessChanged(lv_event_t *e);
    void OnUnitChanged     (lv_event_t *e);
    void OnMuteChanged     (lv_event_t *e);
    void OnThresholdChanged(lv_event_t *e);
    void OnDbCleanup       (lv_event_t *e);

    /* 动画回调（静态成员，可直接转函数指针） */
    static void AnimTempCb     (void *obj, int32_t v);
    static void AnimHumiCb     (void *obj, int32_t v);
    static void AnimDistCb     (void *obj, int32_t v);
    static void AnimLuxCb      (void *obj, int32_t v);
    static void AlertYCb       (void *obj, int32_t v);
    static void AlertHideReadyCb(lv_anim_t *a);

#ifndef SIMULATOR
    static void TouchReadCb(lv_indev_t *indev, lv_indev_data_t *data);
#endif

    void start_value_anim(lv_obj_t *label, lv_anim_exec_xcb_t cb,
                          int32_t *cur, int32_t target);

    /* ── 成员变量（原 g_xxx 全局） ───────────────────────────── */
    app_settings_t settings_{};
    SensorCache    cache_{};
    pthread_mutex_t mutex_ = PTHREAD_MUTEX_INITIALIZER;

    /* 总览 Tab */
    lv_obj_t *label_temp_{};
    lv_obj_t *label_humidity_{};
    lv_obj_t *label_comfort_val_{};
    lv_obj_t *label_comfort_hi_{};
    lv_obj_t *label_pir_{};
    lv_obj_t *label_dist_{};
    lv_obj_t *label_lux_{};
    lv_obj_t *label_accel_{};
    lv_obj_t *label_amag_{};
    lv_obj_t *arc_temp_{};
    lv_obj_t *arc_humi_{};
    lv_obj_t *arc_dist_{};
    lv_obj_t *arc_lux_{};
    lv_obj_t *spark_temp_{};
    lv_obj_t *spark_dist_{};
    lv_obj_t *spark_lux_{};
    lv_obj_t *spark_amag_{};
    lv_obj_t *ov_card_dht_{};
    lv_obj_t *ov_card_dist_{};
    lv_obj_t *ov_card_accel_{};
    lv_obj_t *ov_card_light_{};

    /* 数值动画目标（×10） */
    int32_t anim_temp_{};
    int32_t anim_humi_{};
    int32_t anim_dist_{};
    int32_t anim_lux_{};

    /* 趋势 Tab */
    lv_obj_t           *chart_temp_{};
    lv_chart_series_t  *ser_temp_{};
    lv_obj_t           *chart_humi_{};
    lv_chart_series_t  *ser_humi_{};
    lv_obj_t           *chart_dist_{};
    lv_chart_series_t  *ser_dist_{};
    lv_obj_t           *chart_lux_{};
    lv_chart_series_t  *ser_lux_{};
    lv_obj_t           *chart_amag_{};
    lv_chart_series_t  *ser_amag_{};
    lv_obj_t           *trend_cards_[5]{};
    ChartMeta           chart_meta_[5]{};

    /* 设置 Tab */
    lv_obj_t *label_mqtt_val_{};
    lv_obj_t *label_db_val_{};
    lv_obj_t *label_db_count_{};
    lv_obj_t *label_sysinfo_{};
    lv_obj_t *slider_brightness_{};
    lv_obj_t *slider_threshold_{};
    lv_obj_t *sw_unit_{};
    lv_obj_t *sw_mute_{};

    /* 全屏详情层 */
    lv_obj_t          *detail_panel_{};
    lv_obj_t          *detail_chart_{};
    lv_chart_series_t *detail_ser_{};
    lv_obj_t          *detail_title_{};

    /* 告警横幅 */
    lv_obj_t *panel_alert_{};
    lv_obj_t *label_alert_{};

    /* Tab 切换 */
    lv_obj_t *tabview_{};

#ifndef SIMULATOR
    lv_indev_t *touch_indev_{};
#endif
};
