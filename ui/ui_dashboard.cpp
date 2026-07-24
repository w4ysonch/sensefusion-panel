#include <cstdio>
#include <cstring>
#include "ui_dashboard.hpp"
#include "lvgl/src/widgets/chart/lv_chart_private.h"
#include "../storage/db.h"
#include "../ipc/ipc_shm.h"

#ifdef SIMULATOR
#include "../sim/lv_drv_sdl.h"
#else
#include <fcntl.h>
#include <sys/ioctl.h>
#include <linux/kd.h>
#include "lvgl/drivers/display/lv_linux_fbdev.h"
#endif

LV_FONT_DECLARE(lv_font_sf_sc_14);
LV_FONT_DECLARE(lv_font_sf_sc_16);
LV_FONT_DECLARE(lv_font_sf_sc_28);

/* ── 颜色主题 ─────────────────────────────────────────────────── */
#define CLR_BG        lv_color_hex(0x0d1117)
#define CLR_CARD      lv_color_hex(0x161b22)
#define CLR_BORDER    lv_color_hex(0x30363d)
#define CLR_TITLE     lv_color_hex(0x58a6ff)
#define CLR_VALUE     lv_color_hex(0xe6edf3)
#define CLR_SUB       lv_color_hex(0x8b949e)
#define CLR_GREEN     lv_color_hex(0x3fb950)
#define CLR_YELLOW    lv_color_hex(0xd29922)
#define CLR_RED       lv_color_hex(0xf85149)

#define CHART_POINTS        60u
#define ALERT_AUTO_HIDE_TICKS 150u
#define STATUS_REFRESH_TICKS   90u
#define IR_KEY_LEFT   105
#define IR_KEY_RIGHT  106

static const uint32_t COMFORT_COLORS[] = {
    0x58a6ff, 0x3fb950, 0x3fb950, 0xd29922, 0xf85149,
};
static const char *COMFORT_STR[] = {"冷", "凉", "舒适", "热", "酷热"};

/* ── 单例 ─────────────────────────────────────────────────────── */
Dashboard &Dashboard::instance()
{
    static Dashboard s_inst;
    return s_inst;
}

/* ── 工具：style_card / create_card / create_sub_label / create_arc_gauge / create_sparkline ── */

static void style_card(lv_obj_t *obj)
{
    lv_obj_set_style_bg_color(obj, CLR_CARD, 0);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(obj, CLR_BORDER, 0);
    lv_obj_set_style_border_width(obj, 1, 0);
    lv_obj_set_style_radius(obj, 8, 0);
    lv_obj_set_style_pad_all(obj, 12, 0);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
}

static lv_obj_t *create_card(lv_obj_t *parent, const char *title,
                              int32_t x, int32_t y, int32_t w, int32_t h)
{
    lv_obj_t *card = lv_obj_create(parent);
    lv_obj_set_pos(card, x, y);
    lv_obj_set_size(card, w, h);
    style_card(card);
    lv_obj_t *lbl = lv_label_create(card);
    lv_label_set_text(lbl, title);
    lv_obj_set_style_text_color(lbl, CLR_TITLE, 0);
    lv_obj_set_style_text_font(lbl, &lv_font_sf_sc_14, 0);
    lv_obj_align(lbl, LV_ALIGN_TOP_LEFT, 0, 0);
    return card;
}

static lv_obj_t *create_sub_label(lv_obj_t *card, const char *text, int32_t y)
{
    lv_obj_t *lbl = lv_label_create(card);
    lv_label_set_text(lbl, text);
    lv_obj_set_style_text_color(lbl, CLR_SUB, 0);
    lv_obj_set_style_text_font(lbl, &lv_font_sf_sc_14, 0);
    lv_obj_align(lbl, LV_ALIGN_TOP_LEFT, 0, y);
    return lbl;
}

static lv_obj_t *create_arc_gauge(lv_obj_t *card, int32_t min, int32_t max,
                                   lv_color_t color, int32_t size,
                                   int32_t x, int32_t y)
{
    lv_obj_t *arc = lv_arc_create(card);
    lv_obj_set_size(arc, size, size);
    lv_obj_set_pos(arc, x, y);
    lv_arc_set_rotation(arc, 135);
    lv_arc_set_bg_angles(arc, 0, 270);
    lv_arc_set_range(arc, min, max);
    lv_arc_set_value(arc, min);
    lv_obj_set_style_arc_color(arc, color, LV_PART_INDICATOR);
    lv_obj_set_style_arc_width(arc, 8, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(arc, CLR_BORDER, LV_PART_MAIN);
    lv_obj_set_style_arc_width(arc, 8, LV_PART_MAIN);
    lv_obj_remove_style(arc, nullptr, LV_PART_KNOB);
    lv_obj_clear_flag(arc, LV_OBJ_FLAG_CLICKABLE);
    return arc;
}

static lv_obj_t *create_sparkline(lv_obj_t *card,
                                   int32_t y_min, int32_t y_max,
                                   lv_color_t color,
                                   lv_chart_series_t *src_ser)
{
    lv_obj_t *chart = lv_chart_create(card);
    lv_obj_set_size(chart, lv_obj_get_width(card) - 24, 52);
    lv_obj_align(chart, LV_ALIGN_BOTTOM_MID, 0, -2);
    lv_chart_set_type(chart, LV_CHART_TYPE_LINE);
    lv_chart_set_point_count(chart, CHART_POINTS);
    lv_chart_set_range(chart, LV_CHART_AXIS_PRIMARY_Y, y_min, y_max);
    lv_obj_set_style_bg_opa(chart, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(chart, 0, 0);
    lv_obj_set_style_pad_all(chart, 0, 0);
    lv_obj_set_style_size(chart, 0, 0, LV_PART_INDICATOR);
    lv_obj_set_style_line_width(chart, 2, LV_PART_ITEMS);
    lv_obj_clear_flag(chart, LV_OBJ_FLAG_CLICKABLE);
    lv_chart_series_t *ser = lv_chart_add_series(chart, color, LV_CHART_AXIS_PRIMARY_Y);
    lv_chart_set_series_ext_y_array(chart, ser, src_ser->y_points);
    return chart;
}

static lv_obj_t *create_chart(lv_obj_t *parent, const char *title,
                               int32_t x, int32_t y, int32_t w, int32_t h,
                               int32_t y_min, int32_t y_max,
                               lv_color_t color,
                               lv_chart_series_t **ser_out,
                               lv_obj_t **chart_out)
{
    lv_obj_t *card = lv_obj_create(parent);
    lv_obj_set_pos(card, x, y);
    lv_obj_set_size(card, w, h);
    style_card(card);
    lv_obj_t *lbl = lv_label_create(card);
    lv_label_set_text(lbl, title);
    lv_obj_set_style_text_color(lbl, CLR_TITLE, 0);
    lv_obj_set_style_text_font(lbl, &lv_font_sf_sc_14, 0);
    lv_obj_align(lbl, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_t *chart = lv_chart_create(card);
    lv_obj_set_size(chart, w - 24, h - 44);
    lv_obj_align(chart, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_chart_set_type(chart, LV_CHART_TYPE_LINE);
    lv_chart_set_point_count(chart, CHART_POINTS);
    lv_chart_set_range(chart, LV_CHART_AXIS_PRIMARY_Y, y_min, y_max);
    lv_obj_set_style_bg_color(chart, CLR_CARD, 0);
    lv_obj_set_style_bg_opa(chart, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(chart, CLR_BORDER, 0);
    lv_obj_set_style_border_width(chart, 1, 0);
    lv_obj_set_style_size(chart, 0, 0, LV_PART_INDICATOR);
    lv_chart_series_t *ser = lv_chart_add_series(chart, color, LV_CHART_AXIS_PRIMARY_Y);
    lv_chart_set_all_value(chart, ser, (y_min + y_max) / 2);
    if (ser_out)   *ser_out   = ser;
    if (chart_out) *chart_out = chart;
    lv_obj_clear_flag(chart, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(lbl,   LV_OBJ_FLAG_EVENT_BUBBLE);
    lv_obj_add_flag(chart, LV_OBJ_FLAG_EVENT_BUBBLE);
    return card;
}

static void set_tabview_tab_font(lv_obj_t *tabview, const lv_font_t *font)
{
    lv_obj_t *bar = lv_tabview_get_tab_bar(tabview);
    uint32_t n = lv_obj_get_child_count(bar);
    for (uint32_t i = 0; i < n; i++) {
        lv_obj_t *btn = lv_obj_get_child(bar, static_cast<int32_t>(i));
        lv_obj_t *lbl = lv_obj_get_child(btn, 0);
        if (!lbl) continue;
        lv_obj_set_style_text_font(lbl, font, 0);
        lv_obj_set_style_text_color(lbl, CLR_VALUE, 0);
        lv_obj_set_style_text_color(lbl, CLR_TITLE, LV_STATE_CHECKED);
    }
}

/* ── 动画回调（静态成员） ─────────────────────────────────────── */

void Dashboard::AnimTempCb(void *obj, int32_t v)
{
    Dashboard &d = instance();
    float c = static_cast<float>(v) / 10.0f;
    if (d.settings_.unit_fahrenheit)
        lv_label_set_text_fmt(static_cast<lv_obj_t *>(obj), "%.1f°F", c * 1.8f + 32.0f);
    else
        lv_label_set_text_fmt(static_cast<lv_obj_t *>(obj), "%.1f°C", c);
    if (d.arc_temp_) lv_arc_set_value(d.arc_temp_, v);
}

void Dashboard::AnimHumiCb(void *obj, int32_t v)
{
    Dashboard &d = instance();
    lv_label_set_text_fmt(static_cast<lv_obj_t *>(obj), "%.0f %%RH",
                          static_cast<float>(v) / 10.0f);
    if (d.arc_humi_) lv_arc_set_value(d.arc_humi_, v);
}

void Dashboard::AnimDistCb(void *obj, int32_t v)
{
    Dashboard &d = instance();
    lv_label_set_text_fmt(static_cast<lv_obj_t *>(obj), "%.1f cm",
                          static_cast<float>(v) / 10.0f);
    if (d.arc_dist_) lv_arc_set_value(d.arc_dist_, v);
}

void Dashboard::AnimLuxCb(void *obj, int32_t v)
{
    Dashboard &d = instance();
    lv_label_set_text_fmt(static_cast<lv_obj_t *>(obj), "%d lux", v);
    if (d.arc_lux_) lv_arc_set_value(d.arc_lux_, v);
}

void Dashboard::AlertYCb(void *obj, int32_t v)
{
    lv_obj_set_y(static_cast<lv_obj_t *>(obj), v);
}

void Dashboard::AlertHideReadyCb(lv_anim_t *a)
{
    lv_obj_add_flag(static_cast<lv_obj_t *>(a->var), LV_OBJ_FLAG_HIDDEN);
}

#ifndef SIMULATOR
void Dashboard::TouchReadCb(lv_indev_t *indev, lv_indev_data_t *data)
{
    (void)indev;
    Dashboard &d = instance();
    pthread_mutex_lock(&d.mutex_);
    data->point.x = d.cache_.touch_x;
    data->point.y = d.cache_.touch_y;
    data->state   = d.cache_.touch_pressed ? LV_INDEV_STATE_PRESSED
                                           : LV_INDEV_STATE_RELEASED;
    d.cache_.touch_pressed = 0;
    pthread_mutex_unlock(&d.mutex_);
}
#endif

/* ── start_value_anim ─────────────────────────────────────────── */
void Dashboard::start_value_anim(lv_obj_t *label, lv_anim_exec_xcb_t cb,
                                  int32_t *cur, int32_t target)
{
    if (*cur == target) return;
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, label);
    lv_anim_set_exec_cb(&a, cb);
    lv_anim_set_values(&a, *cur, target);
    lv_anim_set_duration(&a, 400);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
    lv_anim_start(&a);
    *cur = target;
}

/* ── 事件回调（成员函数） ─────────────────────────────────────── */

void Dashboard::OnDetailClose(lv_event_t *e)
{
    (void)e;
    lv_obj_add_flag(detail_panel_, LV_OBJ_FLAG_HIDDEN);
}

void Dashboard::OnChartCardClick(lv_event_t *e)
{
    /* user_data = this；从 target obj 找对应 meta */
    lv_obj_t *card = static_cast<lv_obj_t *>(lv_event_get_target(e));
    ChartMeta *meta = nullptr;
    for (int i = 0; i < 5; i++) {
        if (trend_cards_[i] == card) { meta = &chart_meta_[i]; break; }
    }
    if (!meta) return;

    lv_label_set_text(detail_title_, meta->title);
    lv_chart_set_range(detail_chart_, LV_CHART_AXIS_PRIMARY_Y, meta->y_min, meta->y_max);
    lv_chart_set_series_color(detail_chart_, detail_ser_, meta->color);
    lv_chart_set_series_ext_y_array(detail_chart_, detail_ser_, meta->src_ser->y_points);
    lv_chart_refresh(detail_chart_);
    lv_obj_clear_flag(detail_panel_, LV_OBJ_FLAG_HIDDEN);
}

void Dashboard::OnBrightnessChanged(lv_event_t *e)
{
    (void)e;
    settings_.brightness = static_cast<uint8_t>(lv_slider_get_value(slider_brightness_));
    settings_save(&settings_);
#ifndef SIMULATOR
    static const char *path = "/sys/class/backlight/backlight/brightness";
    FILE *f = fopen(path, "w");
    if (f) { fprintf(f, "%u\n", static_cast<unsigned>(settings_.brightness * 255u / 100u)); fclose(f); }
#endif
}

void Dashboard::OnUnitChanged(lv_event_t *e)
{
    (void)e;
    settings_.unit_fahrenheit = lv_obj_has_state(sw_unit_, LV_STATE_CHECKED) ? 1 : 0;
    settings_save(&settings_);
}

void Dashboard::OnMuteChanged(lv_event_t *e)
{
    (void)e;
    settings_.alert_muted = lv_obj_has_state(sw_mute_, LV_STATE_CHECKED) ? 1 : 0;
    settings_save(&settings_);
}

void Dashboard::OnThresholdChanged(lv_event_t *e)
{
    (void)e;
    float thr = static_cast<float>(lv_slider_get_value(slider_threshold_)) / 100.0f;
    settings_.anomaly_threshold = thr;
    settings_save(&settings_);
    ipc_shm_write_settings(&settings_);
}

void Dashboard::OnDbCleanup(lv_event_t *e)
{
    (void)e;
    db_cleanup_old(30);
    int64_t n = db_count();
    if (label_db_count_) {
        if (n >= 0)
            lv_label_set_text_fmt(label_db_count_,
                "已清理 30 天前记录  当前: %lld 条", static_cast<long long>(n));
        else
            lv_label_set_text(label_db_count_, "已清理 30 天前记录");
    }
}

/* ── build_detail_panel ───────────────────────────────────────── */
void Dashboard::build_detail_panel(lv_obj_t *scr)
{
    detail_panel_ = lv_obj_create(scr);
    lv_obj_set_size(detail_panel_, 1024, 600);
    lv_obj_set_pos(detail_panel_, 0, 0);
    lv_obj_set_style_bg_color(detail_panel_, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(detail_panel_, LV_OPA_80, 0);
    lv_obj_set_style_border_width(detail_panel_, 0, 0);
    lv_obj_set_style_radius(detail_panel_, 0, 0);
    lv_obj_set_style_pad_all(detail_panel_, 0, 0);
    lv_obj_clear_flag(detail_panel_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(detail_panel_, LV_OBJ_FLAG_HIDDEN);

    lv_obj_t *card = lv_obj_create(detail_panel_);
    lv_obj_set_size(card, 960, 520);
    lv_obj_align(card, LV_ALIGN_CENTER, 0, 0);
    style_card(card);

    detail_title_ = lv_label_create(card);
    lv_label_set_text(detail_title_, "");
    lv_obj_set_style_text_color(detail_title_, CLR_TITLE, 0);
    lv_obj_set_style_text_font(detail_title_, &lv_font_sf_sc_16, 0);
    lv_obj_align(detail_title_, LV_ALIGN_TOP_LEFT, 0, 0);

    lv_obj_t *btn_close = lv_button_create(card);
    lv_obj_set_size(btn_close, 60, 32);
    lv_obj_align(btn_close, LV_ALIGN_TOP_RIGHT, 0, -4);
    lv_obj_set_style_bg_color(btn_close, CLR_BORDER, 0);
    lv_obj_set_style_bg_opa(btn_close, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(btn_close, 0, 0);
    lv_obj_set_style_radius(btn_close, 6, 0);
    lv_obj_add_event_cb(btn_close,
        LvglMemberEventThunk<Dashboard, &Dashboard::OnDetailClose>,
        LV_EVENT_CLICKED, this);
    lv_obj_t *lbl_close = lv_label_create(btn_close);
    lv_label_set_text(lbl_close, "关闭");
    lv_obj_set_style_text_font(lbl_close, &lv_font_sf_sc_14, 0);
    lv_obj_set_style_text_color(lbl_close, CLR_VALUE, 0);
    lv_obj_center(lbl_close);

    detail_chart_ = lv_chart_create(card);
    lv_obj_set_size(detail_chart_, 912, 440);
    lv_obj_align(detail_chart_, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_chart_set_type(detail_chart_, LV_CHART_TYPE_LINE);
    lv_chart_set_point_count(detail_chart_, CHART_POINTS);
    lv_obj_set_style_bg_color(detail_chart_, CLR_CARD, 0);
    lv_obj_set_style_bg_opa(detail_chart_, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(detail_chart_, CLR_BORDER, 0);
    lv_obj_set_style_border_width(detail_chart_, 1, 0);
    lv_obj_set_style_size(detail_chart_, 0, 0, LV_PART_INDICATOR);
    detail_ser_ = lv_chart_add_series(detail_chart_,
                      lv_palette_main(LV_PALETTE_BLUE),
                      LV_CHART_AXIS_PRIMARY_Y);
}

/* ── build_tab_overview ───────────────────────────────────────── */
void Dashboard::build_tab_overview(lv_obj_t *tab)
{
#define OV_H1   270
#define OV_H2   232
#define OV_W4   244
#define OV_W2   500
#define OV_GAP  8

    lv_obj_t *card_dht = create_card(tab, "温湿度  DHT11",
        OV_GAP, OV_GAP, OV_W4, OV_H1);

    label_temp_ = lv_label_create(card_dht);
    lv_label_set_text(label_temp_, "--.-°C");
    lv_obj_set_style_text_color(label_temp_, CLR_VALUE, 0);
    lv_obj_set_style_text_font(label_temp_, &lv_font_sf_sc_28, 0);
    lv_obj_set_pos(label_temp_, 0, 24);

    label_humidity_ = lv_label_create(card_dht);
    lv_label_set_text(label_humidity_, "--.- %RH");
    lv_obj_set_style_text_color(label_humidity_, CLR_SUB, 0);
    lv_obj_set_style_text_font(label_humidity_, &lv_font_sf_sc_14, 0);
    lv_obj_set_pos(label_humidity_, 0, 68);

    arc_temp_ = create_arc_gauge(card_dht, -100, 500,
        lv_color_hex(0xf85149), 70, OV_W4 - 70 - 12 - 12, 20);
    arc_humi_ = create_arc_gauge(card_dht, 0, 1000,
        lv_color_hex(0x58a6ff), 56, OV_W4 - 56 - 12 - 12, 96);

    lv_obj_t *card_comfort = create_card(tab, "体感舒适度",
        OV_GAP*2 + OV_W4, OV_GAP, OV_W4, OV_H1);

    label_comfort_val_ = lv_label_create(card_comfort);
    lv_label_set_text(label_comfort_val_, "---");
    lv_obj_set_style_text_color(label_comfort_val_, CLR_VALUE, 0);
    lv_obj_set_style_text_font(label_comfort_val_, &lv_font_sf_sc_28, 0);
    lv_obj_set_pos(label_comfort_val_, 0, 24);

    label_comfort_hi_ = lv_label_create(card_comfort);
    lv_label_set_text(label_comfort_hi_, "HI: --.-°C");
    lv_obj_set_style_text_color(label_comfort_hi_, CLR_SUB, 0);
    lv_obj_set_style_text_font(label_comfort_hi_, &lv_font_sf_sc_14, 0);
    lv_obj_set_pos(label_comfort_hi_, 0, 68);

    static const struct { const char *label; uint32_t color; } levels[] = {
        {"冷", 0x58a6ff}, {"凉", 0x3fb950}, {"适", 0x3fb950},
        {"热", 0xd29922}, {"酷热", 0xf85149},
    };
    for (int i = 0; i < 5; i++) {
        lv_obj_t *dot = lv_obj_create(card_comfort);
        lv_obj_set_size(dot, 32, 22);
        lv_obj_set_pos(dot, i * 38, 100);
        lv_obj_set_style_bg_color(dot, lv_color_hex(levels[i].color), 0);
        lv_obj_set_style_bg_opa(dot, LV_OPA_20, 0);
        lv_obj_set_style_border_color(dot, lv_color_hex(levels[i].color), 0);
        lv_obj_set_style_border_width(dot, 1, 0);
        lv_obj_set_style_radius(dot, 4, 0);
        lv_obj_set_style_pad_all(dot, 0, 0);
        lv_obj_clear_flag(dot, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_t *dl = lv_label_create(dot);
        lv_label_set_text(dl, levels[i].label);
        lv_obj_set_style_text_color(dl, lv_color_hex(levels[i].color), 0);
        lv_obj_set_style_text_font(dl, &lv_font_sf_sc_14, 0);
        lv_obj_center(dl);
    }

    lv_obj_t *card_pir = create_card(tab, "人体感应  SR501",
        OV_GAP*3 + OV_W4*2, OV_GAP, OV_W4, OV_H1);

    label_pir_ = lv_label_create(card_pir);
    lv_label_set_text(label_pir_, "---");
    lv_obj_set_style_text_color(label_pir_, CLR_VALUE, 0);
    lv_obj_set_style_text_font(label_pir_, &lv_font_sf_sc_28, 0);
    lv_obj_set_pos(label_pir_, 0, 24);

    lv_obj_t *pir_dot = lv_obj_create(card_pir);
    lv_obj_set_size(pir_dot, 12, 12);
    lv_obj_set_pos(pir_dot, 0, 72);
    lv_obj_set_style_radius(pir_dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(pir_dot, CLR_SUB, 0);
    lv_obj_set_style_border_width(pir_dot, 0, 0);
    lv_obj_set_style_pad_all(pir_dot, 0, 0);
    lv_obj_clear_flag(pir_dot, LV_OBJ_FLAG_SCROLLABLE);
    (void)pir_dot;
    create_sub_label(card_pir, "范围 7m / 120°", 90);
    create_sub_label(card_pir, "红外热释电", 110);

    lv_obj_t *card_dist = create_card(tab, "距离  SR04",
        OV_GAP*4 + OV_W4*3, OV_GAP, OV_W4, OV_H1);

    label_dist_ = lv_label_create(card_dist);
    lv_label_set_text(label_dist_, "-- cm");
    lv_obj_set_style_text_color(label_dist_, CLR_VALUE, 0);
    lv_obj_set_style_text_font(label_dist_, &lv_font_sf_sc_28, 0);
    lv_obj_set_pos(label_dist_, 0, 24);
    create_sub_label(card_dist, "量程 2~400 cm", 68);
    arc_dist_ = create_arc_gauge(card_dist, 0, 3000,
        lv_color_hex(0x3fb950), 70, OV_W4 - 70 - 12 - 12, 20);

    int32_t row2_y = OV_GAP*2 + OV_H1;

    lv_obj_t *card_accel = create_card(tab, "三轴加速度  ADXL345",
        OV_GAP, row2_y, OV_W2, OV_H2);
    label_accel_ = create_sub_label(card_accel,
        "X: --.--g   Y: --.--g   Z: --.--g", 24);
    label_amag_ = lv_label_create(card_accel);
    lv_label_set_text(label_amag_, "幅值: --.-- g");
    lv_obj_set_style_text_color(label_amag_, CLR_VALUE, 0);
    lv_obj_set_style_text_font(label_amag_, &lv_font_sf_sc_28, 0);
    lv_obj_set_pos(label_amag_, 0, 52);
    create_sub_label(card_accel, "ADXL345 三轴 +/-16g", 100);

    lv_obj_t *card_light = create_card(tab, "光照",
        OV_GAP*2 + OV_W2, row2_y, OV_W2, OV_H2);
    label_lux_ = lv_label_create(card_light);
    lv_label_set_text(label_lux_, "---- lux");
    lv_obj_set_style_text_color(label_lux_, CLR_VALUE, 0);
    lv_obj_set_style_text_font(label_lux_, &lv_font_sf_sc_28, 0);
    lv_obj_set_pos(label_lux_, 0, 24);
    create_sub_label(card_light, "室内正常: 300-500 lux", 68);
    arc_lux_ = create_arc_gauge(card_light, 0, 1000,
        lv_color_hex(0xd29922), 70, OV_W2 - 70 - 12 - 12, 20);

    ov_card_dht_   = card_dht;
    ov_card_dist_  = card_dist;
    ov_card_accel_ = card_accel;
    ov_card_light_ = card_light;

#undef OV_H1
#undef OV_H2
#undef OV_W4
#undef OV_W2
#undef OV_GAP
}

/* ── build_tab_trend ──────────────────────────────────────────── */
void Dashboard::build_tab_trend(lv_obj_t *tab)
{
    static const struct {
        const char *title;
        int32_t x, y, w, h, y_min, y_max;
        lv_palette_t palette;
    } cfg[] = {
        { "温度 °C",              8,   8, 325, 260,    0,  500, LV_PALETTE_RED    },
        { "湿度 %RH",           341,   8, 325, 260,    0, 1000, LV_PALETTE_BLUE   },
        { "距离 cm",             674,  8, 334, 260,    0, 3000, LV_PALETTE_GREEN  },
        { "光照 lux",              8, 276, 496, 260,   0, 1000, LV_PALETTE_YELLOW },
        { "加速度幅值 (x100 g)", 512, 276, 496, 260,   0,  300, LV_PALETTE_PURPLE },
    };

    lv_chart_series_t **sers[]  = { &ser_temp_, &ser_humi_, &ser_dist_, &ser_lux_, &ser_amag_ };
    lv_obj_t         **charts[] = { &chart_temp_, &chart_humi_, &chart_dist_, &chart_lux_, &chart_amag_ };

    for (int i = 0; i < 5; i++) {
        lv_color_t color = lv_palette_main(cfg[i].palette);
        trend_cards_[i] = create_chart(tab, cfg[i].title,
            cfg[i].x, cfg[i].y, cfg[i].w, cfg[i].h,
            cfg[i].y_min, cfg[i].y_max, color,
            sers[i], charts[i]);

        chart_meta_[i] = ChartMeta{
            *charts[i], *sers[i],
            cfg[i].y_min, cfg[i].y_max,
            color, cfg[i].title
        };

        lv_obj_add_flag(trend_cards_[i], LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(trend_cards_[i],
            LvglMemberEventThunk<Dashboard, &Dashboard::OnChartCardClick>,
            LV_EVENT_CLICKED, this);
    }
}

/* ── build_tab_settings ───────────────────────────────────────── */
void Dashboard::build_tab_settings(lv_obj_t *tab)
{
    lv_obj_t *card_mqtt = create_card(tab, "MQTT", 8, 8, 496, 130);
    create_sub_label(card_mqtt, "主题前缀: sensefusion/<sensor>", 28);
    create_sub_label(card_mqtt, "状态由 sensor_daemon 负责", 50);
    label_mqtt_val_ = lv_label_create(card_mqtt);
    lv_label_set_text(label_mqtt_val_, "daemon 负责");
    lv_obj_set_style_text_color(label_mqtt_val_, CLR_SUB, 0);
    lv_obj_set_style_text_font(label_mqtt_val_, &lv_font_sf_sc_14, 0);
    lv_obj_align(label_mqtt_val_, LV_ALIGN_TOP_LEFT, 0, 72);

    lv_obj_t *card_db = create_card(tab, "SQLite 数据库", 512, 8, 496, 130);
#ifdef SIMULATOR
    create_sub_label(card_db, "路径: ./sensefusion.db", 28);
#else
    create_sub_label(card_db, "路径: /var/lib/sensefusion/data.db", 28);
#endif
    label_db_val_ = lv_label_create(card_db);
    lv_label_set_text(label_db_val_, "---");
    lv_obj_set_style_text_color(label_db_val_, CLR_VALUE, 0);
    lv_obj_set_style_text_font(label_db_val_, &lv_font_sf_sc_14, 0);
    lv_obj_align(label_db_val_, LV_ALIGN_TOP_LEFT, 0, 72);

    lv_obj_t *card_ctrl = create_card(tab, "调节", 8, 146, 1008, 330);

    create_sub_label(card_ctrl, "背光亮度", 28);
    slider_brightness_ = lv_slider_create(card_ctrl);
    lv_obj_set_size(slider_brightness_, 800, 20);
    lv_obj_align(slider_brightness_, LV_ALIGN_TOP_LEFT, 0, 54);
    lv_slider_set_range(slider_brightness_, 0, 100);
    lv_slider_set_value(slider_brightness_, settings_.brightness, LV_ANIM_OFF);
    lv_obj_add_event_cb(slider_brightness_,
        LvglMemberEventThunk<Dashboard, &Dashboard::OnBrightnessChanged>,
        LV_EVENT_VALUE_CHANGED, this);

    create_sub_label(card_ctrl, "温度单位  °C / °F", 100);
    sw_unit_ = lv_switch_create(card_ctrl);
    lv_obj_align(sw_unit_, LV_ALIGN_TOP_LEFT, 0, 122);
    if (settings_.unit_fahrenheit) lv_obj_add_state(sw_unit_, LV_STATE_CHECKED);
    lv_obj_add_event_cb(sw_unit_,
        LvglMemberEventThunk<Dashboard, &Dashboard::OnUnitChanged>,
        LV_EVENT_VALUE_CHANGED, this);

    lv_obj_t *lbl_mute_title = lv_label_create(card_ctrl);
    lv_label_set_text(lbl_mute_title, "异常告警静音");
    lv_obj_set_style_text_color(lbl_mute_title, CLR_SUB, 0);
    lv_obj_set_style_text_font(lbl_mute_title, &lv_font_sf_sc_14, 0);
    lv_obj_align(lbl_mute_title, LV_ALIGN_TOP_LEFT, 300, 100);
    sw_mute_ = lv_switch_create(card_ctrl);
    lv_obj_align(sw_mute_, LV_ALIGN_TOP_LEFT, 300, 122);
    if (settings_.alert_muted) lv_obj_add_state(sw_mute_, LV_STATE_CHECKED);
    lv_obj_add_event_cb(sw_mute_,
        LvglMemberEventThunk<Dashboard, &Dashboard::OnMuteChanged>,
        LV_EVENT_VALUE_CHANGED, this);

    create_sub_label(card_ctrl, "异常检测阈值 (g)", 188);
    slider_threshold_ = lv_slider_create(card_ctrl);
    lv_obj_set_size(slider_threshold_, 800, 20);
    lv_obj_align(slider_threshold_, LV_ALIGN_TOP_LEFT, 0, 214);
    lv_slider_set_range(slider_threshold_, 10, 100);
    lv_slider_set_value(slider_threshold_,
        static_cast<int32_t>(settings_.anomaly_threshold * 100.0f), LV_ANIM_OFF);
    lv_obj_add_event_cb(slider_threshold_,
        LvglMemberEventThunk<Dashboard, &Dashboard::OnThresholdChanged>,
        LV_EVENT_VALUE_CHANGED, this);
}

/* ── build_tab_system ─────────────────────────────────────────── */
void Dashboard::build_tab_system(lv_obj_t *tab)
{
    lv_obj_t *card_sys = create_card(tab, "系统信息", 8, 8, 1008, 140);
    label_sysinfo_ = lv_label_create(card_sys);
    lv_label_set_text(label_sysinfo_, "加载中...");
    lv_obj_set_style_text_color(label_sysinfo_, CLR_VALUE, 0);
    lv_obj_set_style_text_font(label_sysinfo_, &lv_font_sf_sc_14, 0);
    lv_obj_align(label_sysinfo_, LV_ALIGN_TOP_LEFT, 0, 26);
    lv_label_set_long_mode(label_sysinfo_, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(label_sysinfo_, 984);

    lv_obj_t *card_clean = create_card(tab, "数据库清理", 8, 156, 1008, 130);
    create_sub_label(card_clean, "清理指定天数前的历史记录, 释放磁盘空间", 28);
    lv_obj_t *btn_clean = lv_button_create(card_clean);
    lv_obj_set_size(btn_clean, 140, 36);
    lv_obj_align(btn_clean, LV_ALIGN_TOP_LEFT, 0, 58);
    lv_obj_set_style_bg_color(btn_clean, CLR_BORDER, 0);
    lv_obj_t *lbl_btn = lv_label_create(btn_clean);
    lv_label_set_text(lbl_btn, "清理 30 天前");
    lv_obj_set_style_text_font(lbl_btn, &lv_font_sf_sc_14, 0);
    lv_obj_center(lbl_btn);
    lv_obj_add_event_cb(btn_clean,
        LvglMemberEventThunk<Dashboard, &Dashboard::OnDbCleanup>,
        LV_EVENT_CLICKED, this);
    label_db_count_ = lv_label_create(card_clean);
    lv_label_set_text(label_db_count_, "点击按钮执行清理");
    lv_obj_set_style_text_color(label_db_count_, CLR_SUB, 0);
    lv_obj_set_style_text_font(label_db_count_, &lv_font_sf_sc_14, 0);
    lv_obj_align(label_db_count_, LV_ALIGN_TOP_LEFT, 160, 68);

    lv_obj_t *card_ir = create_card(tab, "红外遥控", 8, 294, 1008, 64);
    create_sub_label(card_ir,
        "KEY_LEFT / KEY_RIGHT  --  循环切换 Tab [总览/趋势/设置/系统]", 28);
}

/* ── refresh_sysinfo ─────────────────────────────────────────── */
void Dashboard::refresh_sysinfo()
{
    if (!label_sysinfo_) return;
    char cpu[64] = "未知";
    FILE *f = fopen("/proc/cpuinfo", "r");
    if (f) {
        char line[128];
        while (fgets(line, sizeof(line), f)) {
            if (strncmp(line, "Hardware", 8) == 0) {
                char *p = strchr(line, ':');
                if (p) { p += 2; line[strcspn(line, "\n")] = 0; snprintf(cpu, sizeof(cpu), "%s", p); }
                break;
            }
        }
        fclose(f);
    }
    unsigned long mem_total = 0, mem_free = 0;
    f = fopen("/proc/meminfo", "r");
    if (f) {
        char key[32]; unsigned long val;
        while (fscanf(f, "%31s %lu %*s\n", key, &val) == 2) {
            if (strcmp(key, "MemTotal:") == 0) mem_total = val;
            else if (strcmp(key, "MemAvailable:") == 0) { mem_free = val; break; }
        }
        fclose(f);
    }
    double uptime = 0.0;
    f = fopen("/proc/uptime", "r");
    if (f) { fscanf(f, "%lf", &uptime); fclose(f); }
    unsigned long up_h = static_cast<unsigned long>(uptime) / 3600;
    unsigned long up_m = (static_cast<unsigned long>(uptime) % 3600) / 60;
    char buf[200];
    snprintf(buf, sizeof(buf),
        "CPU: %s\n内存: %lu MB 总 / %lu MB 可用\n运行时间: %luh %02lum",
        cpu, mem_total / 1024, mem_free / 1024, up_h, up_m);
    lv_label_set_text(label_sysinfo_, buf);
}

/* ── alert 动画 ───────────────────────────────────────────────── */
void Dashboard::alert_slide_in()
{
    lv_obj_set_y(panel_alert_, 600);
    lv_obj_clear_flag(panel_alert_, LV_OBJ_FLAG_HIDDEN);
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, panel_alert_);
    lv_anim_set_exec_cb(&a, AlertYCb);
    lv_anim_set_values(&a, 600, 540);
    lv_anim_set_duration(&a, 300);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
    lv_anim_start(&a);
}

void Dashboard::alert_slide_out()
{
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, panel_alert_);
    lv_anim_set_exec_cb(&a, AlertYCb);
    lv_anim_set_values(&a, 540, 600);
    lv_anim_set_duration(&a, 250);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_in);
    lv_anim_set_ready_cb(&a, AlertHideReadyCb);
    lv_anim_start(&a);
}

/* ── build_ui ─────────────────────────────────────────────────── */
void Dashboard::build_ui()
{
    lv_obj_t *scr = lv_screen_active();
    lv_obj_set_style_bg_color(scr, CLR_BG, 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);

    tabview_ = lv_tabview_create(scr);
    lv_obj_set_size(tabview_, 1024, 600);
    lv_obj_set_pos(tabview_, 0, 0);
    lv_tabview_set_tab_bar_position(tabview_, LV_DIR_TOP);
    lv_tabview_set_tab_bar_size(tabview_, 44);
    lv_obj_set_style_bg_color(tabview_, CLR_BG, 0);
    lv_obj_set_style_bg_opa(tabview_, LV_OPA_COVER, 0);

    lv_obj_t *tab_bar = lv_tabview_get_tab_bar(tabview_);
    lv_obj_set_style_bg_color(tab_bar, CLR_CARD, 0);
    lv_obj_set_style_bg_opa(tab_bar, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(tab_bar, 0, LV_PART_MAIN);

    lv_obj_t *tab_overview = lv_tabview_add_tab(tabview_, "  总览  ");
    lv_obj_t *tab_trend    = lv_tabview_add_tab(tabview_, "  趋势  ");
    lv_obj_t *tab_settings = lv_tabview_add_tab(tabview_, "  设置  ");
    lv_obj_t *tab_system   = lv_tabview_add_tab(tabview_, "  系统  ");

    set_tabview_tab_font(tabview_, &lv_font_sf_sc_16);

    lv_obj_t *tabs[4] = {tab_overview, tab_trend, tab_settings, tab_system};
    for (int i = 0; i < 4; i++) {
        lv_obj_set_style_bg_color(tabs[i], CLR_BG, 0);
        lv_obj_set_style_bg_opa(tabs[i], LV_OPA_COVER, 0);
        lv_obj_set_style_pad_all(tabs[i], 0, 0);
        lv_obj_set_style_border_width(tabs[i], 0, 0);
        lv_obj_clear_flag(tabs[i], LV_OBJ_FLAG_SCROLLABLE);
    }

    build_tab_overview(tab_overview);
    build_tab_trend(tab_trend);
    build_tab_settings(tab_settings);
    build_tab_system(tab_system);
    refresh_sysinfo();

    lv_obj_update_layout(scr);
    spark_temp_ = create_sparkline(ov_card_dht_,   0,  500, lv_color_hex(0xf85149), ser_temp_);
    spark_dist_ = create_sparkline(ov_card_dist_,  0, 3000, lv_color_hex(0x3fb950), ser_dist_);
    spark_lux_  = create_sparkline(ov_card_light_, 0, 1000, lv_color_hex(0xd29922), ser_lux_);
    spark_amag_ = create_sparkline(ov_card_accel_, 0,  300, lv_color_hex(0xa371f7), ser_amag_);

    build_detail_panel(scr);

    panel_alert_ = lv_obj_create(scr);
    lv_obj_set_pos(panel_alert_, 8, 540);
    lv_obj_set_size(panel_alert_, 1008, 52);
    lv_obj_set_style_bg_color(panel_alert_, CLR_RED, 0);
    lv_obj_set_style_bg_opa(panel_alert_, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(panel_alert_, 0, 0);
    lv_obj_set_style_radius(panel_alert_, 6, 0);
    lv_obj_add_flag(panel_alert_, LV_OBJ_FLAG_HIDDEN);

    label_alert_ = lv_label_create(panel_alert_);
    lv_label_set_text(label_alert_, "告警");
    lv_obj_set_style_text_color(label_alert_, CLR_VALUE, 0);
    lv_obj_set_style_text_font(label_alert_, &lv_font_sf_sc_16, 0);
    lv_obj_align(label_alert_, LV_ALIGN_LEFT_MID, 12, 0);
}

/* ── 公开接口 ─────────────────────────────────────────────────── */
void Dashboard::init(const app_settings_t *settings)
{
    settings_ = *settings;
#ifdef SIMULATOR
    lv_init();
    sdl_hal_init(1024, 600);
    printf("[dashboard] SDL2 模拟器初始化完成\n");
#else
    lv_init();
    lv_display_t *disp = lv_linux_fbdev_create();
    lv_linux_fbdev_set_file(disp, "/dev/fb0");
    int tty_fd = open("/dev/console", O_RDWR);
    if (tty_fd >= 0) { ioctl(tty_fd, KDSETMODE, KD_GRAPHICS); close(tty_fd); }
    printf("[dashboard] FBDEV /dev/fb0 初始化完成\n");
#endif
    build_ui();
#ifndef SIMULATOR
    touch_indev_ = lv_indev_create();
    lv_indev_set_type(touch_indev_, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(touch_indev_, TouchReadCb);
    lv_indev_set_display(touch_indev_, lv_display_get_default());
#endif
}

void Dashboard::update_dht11(float temp, float humidity)
{
    pthread_mutex_lock(&mutex_);
    cache_.temp = temp; cache_.humidity = humidity; cache_.dht11_dirty = true;
    pthread_mutex_unlock(&mutex_);
}

void Dashboard::update_accel(float x, float y, float z, float mag)
{
    pthread_mutex_lock(&mutex_);
    cache_.ax = x; cache_.ay = y; cache_.az = z; cache_.amag = mag;
    cache_.accel_dirty = true;
    pthread_mutex_unlock(&mutex_);
}

void Dashboard::update_pir(uint8_t detected)
{
    pthread_mutex_lock(&mutex_);
    cache_.pir_detected = detected; cache_.pir_dirty = true;
    pthread_mutex_unlock(&mutex_);
}

void Dashboard::update_distance(float cm)
{
    pthread_mutex_lock(&mutex_);
    cache_.dist_cm = cm; cache_.dist_dirty = true;
    pthread_mutex_unlock(&mutex_);
}

void Dashboard::update_light(uint16_t lux)
{
    pthread_mutex_lock(&mutex_);
    cache_.lux = lux; cache_.light_dirty = true;
    pthread_mutex_unlock(&mutex_);
}

void Dashboard::update_comfort(float heat_index, ComfortLevel level)
{
    pthread_mutex_lock(&mutex_);
    cache_.heat_index = heat_index;
    cache_.comfort_level = static_cast<uint8_t>(level);
    cache_.comfort_dirty = true;
    pthread_mutex_unlock(&mutex_);
}

void Dashboard::show_alert(float magnitude)
{
    pthread_mutex_lock(&mutex_);
    cache_.anomaly_mag = magnitude; cache_.anomaly_dirty = true;
    pthread_mutex_unlock(&mutex_);
}

void Dashboard::update_touch(int32_t x, int32_t y, uint8_t pressed)
{
    pthread_mutex_lock(&mutex_);
    cache_.touch_x = x; cache_.touch_y = y;
    cache_.touch_pressed = pressed; cache_.touch_dirty = true;
    pthread_mutex_unlock(&mutex_);
}

void Dashboard::handle_ir_key(uint16_t key_code)
{
    if (key_code == IR_KEY_LEFT || key_code == IR_KEY_RIGHT) {
        pthread_mutex_lock(&mutex_);
        cache_.ir_key = static_cast<int32_t>(key_code); cache_.ir_dirty = true;
        pthread_mutex_unlock(&mutex_);
    } else {
        printf("[dashboard] IR key=0x%04x\n", key_code);
    }
}

/* ── tick ─────────────────────────────────────────────────────── */
uint32_t Dashboard::tick()
{
    static uint32_t alert_ticks  = 0;
    static uint32_t status_ticks = 0;

    SensorCache local;
    pthread_mutex_lock(&mutex_);
    local = cache_;
    cache_.dht11_dirty = cache_.accel_dirty = cache_.pir_dirty   = false;
    cache_.dist_dirty  = cache_.light_dirty = cache_.comfort_dirty = false;
    cache_.anomaly_dirty = cache_.touch_dirty = cache_.ir_dirty   = false;
    pthread_mutex_unlock(&mutex_);

    if (local.dht11_dirty) {
        int32_t t10 = static_cast<int32_t>(local.temp * 10.0f);
        int32_t h10 = static_cast<int32_t>(local.humidity * 10.0f);
        start_value_anim(label_temp_,     AnimTempCb, &anim_temp_, t10);
        start_value_anim(label_humidity_, AnimHumiCb, &anim_humi_, h10);
        lv_chart_set_next_value(chart_temp_, ser_temp_, static_cast<lv_value_precise_t>(t10));
        lv_chart_set_next_value(chart_humi_, ser_humi_, static_cast<lv_value_precise_t>(h10));
        if (spark_temp_) lv_obj_invalidate(spark_temp_);
    }

    if (local.comfort_dirty) {
        uint8_t lvl = local.comfort_level < 5 ? local.comfort_level : 4;
        lv_label_set_text(label_comfort_val_, COMFORT_STR[lvl]);
        lv_obj_set_style_text_color(label_comfort_val_,
                                    lv_color_hex(COMFORT_COLORS[lvl]), 0);
        lv_label_set_text_fmt(label_comfort_hi_, "HI: %.1f°C", local.heat_index);
    }

    if (local.pir_dirty) {
        if (local.pir_detected) {
            lv_label_set_text(label_pir_, "有人");
            lv_obj_set_style_text_color(label_pir_, CLR_YELLOW, 0);
        } else {
            lv_label_set_text(label_pir_, "无人");
            lv_obj_set_style_text_color(label_pir_, CLR_GREEN, 0);
        }
    }

    if (local.dist_dirty) {
        int32_t d10 = static_cast<int32_t>(local.dist_cm * 10.0f);
        start_value_anim(label_dist_, AnimDistCb, &anim_dist_, d10);
        lv_chart_set_next_value(chart_dist_, ser_dist_, static_cast<lv_value_precise_t>(d10));
        if (spark_dist_) lv_obj_invalidate(spark_dist_);
    }

    if (local.light_dirty) {
        int32_t lux = static_cast<int32_t>(local.lux);
        start_value_anim(label_lux_, AnimLuxCb, &anim_lux_, lux);
        lv_chart_set_next_value(chart_lux_, ser_lux_, static_cast<lv_value_precise_t>(lux));
        if (spark_lux_) lv_obj_invalidate(spark_lux_);
    }

    if (local.accel_dirty) {
        lv_label_set_text_fmt(label_accel_,
            "X: %+.2fg   Y: %+.2fg   Z: %+.2fg",
            local.ax, local.ay, local.az);
        lv_label_set_text_fmt(label_amag_, "幅值: %.2f g", local.amag);
        lv_chart_set_next_value(chart_amag_, ser_amag_,
            static_cast<lv_value_precise_t>(local.amag * 100.0f));
        if (spark_amag_) lv_obj_invalidate(spark_amag_);
    }

    if (local.anomaly_dirty && !settings_.alert_muted) {
        lv_label_set_text_fmt(label_alert_,
            "  检测到震动/冲击  幅度 %.3fg", local.anomaly_mag);
        alert_slide_in();
        alert_ticks = ALERT_AUTO_HIDE_TICKS;
    }

    if (local.ir_dirty) {
        uint32_t cur  = lv_tabview_get_tab_active(tabview_);
        uint32_t next = (local.ir_key == IR_KEY_RIGHT)
                        ? (cur + 1) % 4 : (cur + 4 - 1) % 4;
        lv_tabview_set_active(tabview_, next, LV_ANIM_ON);
    }

    if (local.touch_dirty && alert_ticks > 0)
        alert_ticks = 1;

    if (alert_ticks > 0) {
        if (--alert_ticks == 0)
            alert_slide_out();
    }

    if (++status_ticks >= STATUS_REFRESH_TICKS) {
        status_ticks = 0;
        lv_label_set_text(label_db_val_, db_status_str());
        refresh_sysinfo();
    }

    return lv_timer_handler();
}
