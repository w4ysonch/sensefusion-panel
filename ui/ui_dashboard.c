#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <pthread.h>
#include "ui_dashboard.h"
#include "lvgl/lvgl.h"
#include "../storage/db.h"
#include "../network/mqtt_client.h"

#ifdef SIMULATOR
#include "../sim/lv_drv_sdl.h"
#endif

/* ── 颜色主题 ─────────────────────────────────────────────── */
#define CLR_BG        lv_color_hex(0x0d1117)
#define CLR_CARD      lv_color_hex(0x161b22)
#define CLR_BORDER    lv_color_hex(0x30363d)
#define CLR_TITLE     lv_color_hex(0x58a6ff)
#define CLR_VALUE     lv_color_hex(0xe6edf3)
#define CLR_SUB       lv_color_hex(0x8b949e)
#define CLR_GREEN     lv_color_hex(0x3fb950)
#define CLR_YELLOW    lv_color_hex(0xd29922)
#define CLR_RED       lv_color_hex(0xf85149)

static const uint32_t COMFORT_COLORS[] = {
    0x58a6ff, 0x3fb950, 0x3fb950, 0xd29922, 0xf85149,
};
static const char *COMFORT_STR[] = {"冷", "凉", "舒适", "热", "酷热"};

/* ── 趋势图配置 ───────────────────────────────────────────── */
#define CHART_POINTS  60   /* 每条折线保留的数据点数（约 60 s） */

/* ── 线程安全数据缓存 ─────────────────────────────────────── */
typedef struct {
    bool dht11_dirty;
    bool accel_dirty;
    bool pir_dirty;
    bool dist_dirty;
    bool light_dirty;
    bool comfort_dirty;
    bool anomaly_dirty;
    bool touch_dirty;

    float    temp, humidity;
    float    ax, ay, az, amag;
    uint8_t  pir_detected;
    float    dist_cm;
    uint16_t lux;
    float    heat_index;
    uint8_t  comfort_level;
    float    anomaly_mag;
    int32_t  touch_x, touch_y;
} sensor_cache_t;

static sensor_cache_t  g_cache = {0};
static pthread_mutex_t g_mutex = PTHREAD_MUTEX_INITIALIZER;

/* ── LVGL 控件指针 ───────────────────────────────────────── */

/* 总览 Tab */
static lv_obj_t *g_label_temp;
static lv_obj_t *g_label_humidity;
static lv_obj_t *g_label_comfort_val;
static lv_obj_t *g_label_comfort_hi;
static lv_obj_t *g_label_pir;
static lv_obj_t *g_label_dist;
static lv_obj_t *g_label_lux;
static lv_obj_t *g_label_accel;

/* 趋势 Tab */
static lv_obj_t           *g_chart_temp;
static lv_chart_series_t  *g_ser_temp;
static lv_obj_t           *g_chart_humi;
static lv_chart_series_t  *g_ser_humi;
static lv_obj_t           *g_chart_dist;
static lv_chart_series_t  *g_ser_dist;
static lv_obj_t           *g_chart_lux;
static lv_chart_series_t  *g_ser_lux;
static lv_obj_t           *g_chart_amag;
static lv_chart_series_t  *g_ser_amag;

/* 设置 Tab */
static lv_obj_t *g_label_mqtt_val;
static lv_obj_t *g_label_db_val;

/* 告警横幅（浮动在屏幕最上层） */
static lv_obj_t *g_panel_alert;
static lv_obj_t *g_label_alert;

/* Tab 切换（IR 遥控） */
static lv_obj_t *g_tabview;

/* ── 工具函数 ────────────────────────────────────────────── */

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
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_14, 0);
    lv_obj_align(lbl, LV_ALIGN_TOP_LEFT, 0, 0);
    return card;
}

static lv_obj_t *create_value_label(lv_obj_t *card, const char *text, int32_t y)
{
    lv_obj_t *lbl = lv_label_create(card);
    lv_label_set_text(lbl, text);
    lv_obj_set_style_text_color(lbl, CLR_VALUE, 0);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_28, 0);
    lv_obj_align(lbl, LV_ALIGN_TOP_LEFT, 0, y);
    return lbl;
}

static lv_obj_t *create_sub_label(lv_obj_t *card, const char *text, int32_t y)
{
    lv_obj_t *lbl = lv_label_create(card);
    lv_label_set_text(lbl, text);
    lv_obj_set_style_text_color(lbl, CLR_SUB, 0);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_14, 0);
    lv_obj_align(lbl, LV_ALIGN_TOP_LEFT, 0, y);
    return lbl;
}

/* 创建带标题的趋势折线图 */
static lv_obj_t *create_chart(lv_obj_t *parent, const char *title,
                               int32_t x, int32_t y, int32_t w, int32_t h,
                               int32_t y_min, int32_t y_max,
                               lv_color_t color,
                               lv_chart_series_t **ser_out)
{
    lv_obj_t *card = lv_obj_create(parent);
    lv_obj_set_pos(card, x, y);
    lv_obj_set_size(card, w, h);
    style_card(card);

    /* 标题 */
    lv_obj_t *lbl = lv_label_create(card);
    lv_label_set_text(lbl, title);
    lv_obj_set_style_text_color(lbl, CLR_TITLE, 0);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_14, 0);
    lv_obj_align(lbl, LV_ALIGN_TOP_LEFT, 0, 0);

    /* 图表本体 */
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
    /* 去掉默认的点标记，只显示折线 */
    lv_obj_set_style_size(chart, 0, 0, LV_PART_INDICATOR);

    lv_chart_series_t *ser = lv_chart_add_series(
        chart, color, LV_CHART_AXIS_PRIMARY_Y);
    /* 填充初始值为图表中点，避免初始从 0 开始突变 */
    lv_chart_set_all_value(chart, ser, (y_min + y_max) / 2);

    if (ser_out) *ser_out = ser;
    return chart;
}

/* ── 各 Tab 构建 ─────────────────────────────────────────── */

/* 总览 Tab — 沿用原有卡片布局，y 坐标相对于 tab 内容区 */
static void build_tab_overview(lv_obj_t *tab)
{
    /* 第一行（y=8，高 185）*/
    lv_obj_t *card_dht = create_card(tab, "温湿度  DHT11",  8,   8, 185, 185);
    g_label_temp     = create_value_label(card_dht, "--.-°C",    24);
    g_label_humidity = create_sub_label  (card_dht, "--.- %RH",  62);

    lv_obj_t *card_comfort = create_card(tab, "体感舒适度", 201,  8, 185, 185);
    g_label_comfort_val = create_value_label(card_comfort, "---",        24);
    g_label_comfort_hi  = create_sub_label  (card_comfort, "HI: --.-°C", 62);

    lv_obj_t *card_pir = create_card(tab, "人体感应  SR501", 394,  8, 185, 185);
    g_label_pir = create_value_label(card_pir, "---", 24);

    lv_obj_t *card_dist = create_card(tab, "距离  SR04", 587,  8, 205, 185);
    g_label_dist = create_value_label(card_dist, "-- cm", 24);

    /* 第二行（y=201，高 185）*/
    lv_obj_t *card_accel = create_card(tab, "三轴加速度  ADXL345",
                                        8, 201, 570, 185);
    g_label_accel = create_sub_label(card_accel,
        "X: --.--g   Y: --.--g   Z: --.--g   |a|: --.--g", 28);

    lv_obj_t *card_light = create_card(tab, "光照", 586, 201, 206, 185);
    g_label_lux = create_value_label(card_light, "---- lux", 24);
}

/* 趋势 Tab — 5 个 lv_chart（×10 整数化） */
static void build_tab_trend(lv_obj_t *tab)
{
    /* 行 1 (y=8, h=195)：温度 / 湿度 / 距离 */
    g_chart_temp = create_chart(tab, "温度 °C",
        8,   8, 248, 195,   0, 500,   /* 0.0 ~ 50.0 °C，×10 */
        lv_palette_main(LV_PALETTE_RED), &g_ser_temp);

    g_chart_humi = create_chart(tab, "湿度 %RH",
        264, 8, 248, 195,   0, 1000,  /* 0.0 ~ 100.0 % */
        lv_palette_main(LV_PALETTE_BLUE), &g_ser_humi);

    g_chart_dist = create_chart(tab, "距离 cm",
        520, 8, 272, 195,   0, 3000,  /* 0 ~ 300 cm */
        lv_palette_main(LV_PALETTE_GREEN), &g_ser_dist);

    /* 行 2 (y=211, h=195)：光照 / 加速度幅值 */
    g_chart_lux = create_chart(tab, "光照 lux",
        8,   211, 384, 195,  0, 1000,
        lv_palette_main(LV_PALETTE_YELLOW), &g_ser_lux);

    g_chart_amag = create_chart(tab, "加速度幅值 (×100 g)",
        400, 211, 392, 195,  0, 300,   /* 0 ~ 3.0 g，×100 */
        lv_palette_main(LV_PALETTE_PURPLE), &g_ser_amag);
}

/* 设置 Tab */
static void build_tab_settings(lv_obj_t *tab)
{
    /* MQTT 状态卡 */
    lv_obj_t *card_mqtt = create_card(tab, "MQTT", 8, 8, 380, 130);
    create_sub_label(card_mqtt, "主题前缀: " MQTT_TOPIC_PREFIX, 28);
    create_sub_label(card_mqtt, "Broker: mqtt_init() 传入地址", 52);
    g_label_mqtt_val = lv_label_create(card_mqtt);
    lv_label_set_text(g_label_mqtt_val, "---");
    lv_obj_set_style_text_color(g_label_mqtt_val, CLR_VALUE, 0);
    lv_obj_set_style_text_font(g_label_mqtt_val, &lv_font_montserrat_14, 0);
    lv_obj_align(g_label_mqtt_val, LV_ALIGN_TOP_LEFT, 0, 76);

    /* DB 状态卡 */
    lv_obj_t *card_db = create_card(tab, "SQLite 数据库", 396, 8, 396, 130);
#ifdef SIMULATOR
    create_sub_label(card_db, "路径: ./sensefusion.db", 28);
#else
    create_sub_label(card_db, "路径: /var/lib/sensefusion/data.db", 28);
#endif
    create_sub_label(card_db, "日志: 所有传感器每次更新写入", 52);
    g_label_db_val = lv_label_create(card_db);
    lv_label_set_text(g_label_db_val, "---");
    lv_obj_set_style_text_color(g_label_db_val, CLR_VALUE, 0);
    lv_obj_set_style_text_font(g_label_db_val, &lv_font_montserrat_14, 0);
    lv_obj_align(g_label_db_val, LV_ALIGN_TOP_LEFT, 0, 76);

    /* IR 遥控说明 */
    lv_obj_t *card_ir = create_card(tab, "红外遥控", 8, 146, 784, 90);
    create_sub_label(card_ir,
        "KEY_LEFT / KEY_RIGHT  —  切换 Tab（总览 / 趋势 / 设置）", 28);
    create_sub_label(card_ir,
        "模拟器：鼠标点击 Tab 标签切换页面", 52);
}

/* ── 主界面构建入口 ───────────────────────────────────────── */
static void build_ui(void)
{
    lv_obj_t *scr = lv_screen_active();
    lv_obj_set_style_bg_color(scr, CLR_BG, 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);

    /* ── TabView（全屏） ── */
    g_tabview = lv_tabview_create(scr);
    lv_obj_set_size(g_tabview, 800, 480);
    lv_obj_set_pos(g_tabview, 0, 0);
    lv_tabview_set_tab_bar_position(g_tabview, LV_DIR_TOP);
    lv_tabview_set_tab_bar_size(g_tabview, 44);
    lv_obj_set_style_bg_color(g_tabview, CLR_BG, 0);
    lv_obj_set_style_bg_opa(g_tabview, LV_OPA_COVER, 0);

    /* Tab 栏样式 */
    lv_obj_t *tab_bar = lv_tabview_get_tab_bar(g_tabview);
    lv_obj_set_style_bg_color(tab_bar, CLR_CARD, 0);
    lv_obj_set_style_bg_opa(tab_bar, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(tab_bar, 0, LV_PART_MAIN);

    /* 三个页面 */
    lv_obj_t *tab_overview = lv_tabview_add_tab(g_tabview, "  总览  ");
    lv_obj_t *tab_trend    = lv_tabview_add_tab(g_tabview, "  趋势  ");
    lv_obj_t *tab_settings = lv_tabview_add_tab(g_tabview, "  设置  ");

    /* 各 tab 背景与内边距 */
    lv_obj_t *tabs[3] = {tab_overview, tab_trend, tab_settings};
    for (int i = 0; i < 3; i++) {
        lv_obj_set_style_bg_color(tabs[i], CLR_BG, 0);
        lv_obj_set_style_bg_opa(tabs[i], LV_OPA_COVER, 0);
        lv_obj_set_style_pad_all(tabs[i], 0, 0);
        lv_obj_set_style_border_width(tabs[i], 0, 0);
        lv_obj_clear_flag(tabs[i], LV_OBJ_FLAG_SCROLLABLE);
    }

    build_tab_overview(tab_overview);
    build_tab_trend(tab_trend);
    build_tab_settings(tab_settings);

    /* ── 告警横幅：screen 直接子对象，渲染在 tabview 之上 ── */
    g_panel_alert = lv_obj_create(scr);
    lv_obj_set_pos(g_panel_alert, 8, 420);
    lv_obj_set_size(g_panel_alert, 784, 52);
    lv_obj_set_style_bg_color(g_panel_alert, CLR_RED, 0);
    lv_obj_set_style_bg_opa(g_panel_alert, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(g_panel_alert, 0, 0);
    lv_obj_set_style_radius(g_panel_alert, 6, 0);
    lv_obj_add_flag(g_panel_alert, LV_OBJ_FLAG_HIDDEN);

    g_label_alert = lv_label_create(g_panel_alert);
    lv_label_set_text(g_label_alert, "告警");
    lv_obj_set_style_text_color(g_label_alert, CLR_VALUE, 0);
    lv_obj_set_style_text_font(g_label_alert, &lv_font_montserrat_16, 0);
    lv_obj_align(g_label_alert, LV_ALIGN_LEFT_MID, 12, 0);
}

/* ── 公开接口 ────────────────────────────────────────────── */

void dashboard_init(void)
{
#ifdef SIMULATOR
    lv_init();
    sdl_hal_init(800, 480);
    printf("[dashboard] SDL2 模拟器初始化完成\n");
#else
    lv_init();
    /* TODO: 板子 framebuffer HAL */
    printf("[dashboard] 板子 HAL TODO\n");
#endif
    build_ui();
}

/* 以下 update_* 运行在 embedmq 消费者线程，只写缓存 */

void dashboard_update_dht11(float temp, float humidity)
{
    pthread_mutex_lock(&g_mutex);
    g_cache.temp         = temp;
    g_cache.humidity     = humidity;
    g_cache.dht11_dirty  = true;
    pthread_mutex_unlock(&g_mutex);
}

void dashboard_update_accel(float x, float y, float z, float magnitude)
{
    pthread_mutex_lock(&g_mutex);
    g_cache.ax          = x;
    g_cache.ay          = y;
    g_cache.az          = z;
    g_cache.amag        = magnitude;
    g_cache.accel_dirty = true;
    pthread_mutex_unlock(&g_mutex);
}

void dashboard_update_pir(uint8_t detected)
{
    pthread_mutex_lock(&g_mutex);
    g_cache.pir_detected = detected;
    g_cache.pir_dirty    = true;
    pthread_mutex_unlock(&g_mutex);
}

void dashboard_update_distance(float cm)
{
    pthread_mutex_lock(&g_mutex);
    g_cache.dist_cm    = cm;
    g_cache.dist_dirty = true;
    pthread_mutex_unlock(&g_mutex);
}

void dashboard_update_light(uint16_t lux)
{
    pthread_mutex_lock(&g_mutex);
    g_cache.lux         = lux;
    g_cache.light_dirty = true;
    pthread_mutex_unlock(&g_mutex);
}

void dashboard_update_comfort(float heat_index, comfort_level_t level)
{
    pthread_mutex_lock(&g_mutex);
    g_cache.heat_index    = heat_index;
    g_cache.comfort_level = (uint8_t)level;
    g_cache.comfort_dirty = true;
    pthread_mutex_unlock(&g_mutex);
}

void dashboard_show_alert(uint8_t type, float magnitude)
{
    (void)type;
    pthread_mutex_lock(&g_mutex);
    g_cache.anomaly_mag   = magnitude;
    g_cache.anomaly_dirty = true;
    pthread_mutex_unlock(&g_mutex);
}

void dashboard_update_touch(int32_t x, int32_t y)
{
    pthread_mutex_lock(&g_mutex);
    g_cache.touch_x     = x;
    g_cache.touch_y     = y;
    g_cache.touch_dirty = true;
    pthread_mutex_unlock(&g_mutex);
}

/* IR 遥控切换 Tab，运行在 embedmq 消费者线程：只写缓存 */
void dashboard_handle_ir_key(uint16_t key_code)
{
    /* KEY_LEFT=105 KEY_RIGHT=106（linux/input-event-codes.h） */
    if (key_code == 105 || key_code == 106) {
        /* 需要在主线程操作 LVGL，借用 touch_dirty 触发 tick 内切换 */
        pthread_mutex_lock(&g_mutex);
        /* 用负值区分 IR 切换（touch x 通常为正） */
        g_cache.touch_x    = (key_code == 106) ? -2 : -1;
        g_cache.touch_dirty = true;
        pthread_mutex_unlock(&g_mutex);
    } else {
        printf("[dashboard] IR key=0x%04x\n", key_code);
    }
}

/* ── tick：主线程，将缓存刷入 LVGL 控件，再驱动渲染 ── */
#define ALERT_AUTO_HIDE_TICKS 150u   /* ~5 s @ 30 fps */
#define STATUS_REFRESH_TICKS  90u    /* ~3 s 刷新一次设置页状态 */

uint32_t dashboard_tick(void)
{
    static uint32_t alert_ticks   = 0;
    static uint32_t status_ticks  = 0;

    sensor_cache_t local;
    pthread_mutex_lock(&g_mutex);
    local = g_cache;
    g_cache.dht11_dirty   = false;
    g_cache.accel_dirty   = false;
    g_cache.pir_dirty     = false;
    g_cache.dist_dirty    = false;
    g_cache.light_dirty   = false;
    g_cache.comfort_dirty = false;
    g_cache.anomaly_dirty = false;
    g_cache.touch_dirty   = false;
    pthread_mutex_unlock(&g_mutex);

    /* ── 总览 Tab 更新 ── */
    if (local.dht11_dirty) {
        lv_label_set_text_fmt(g_label_temp,     "%.1f°C",    local.temp);
        lv_label_set_text_fmt(g_label_humidity, "%.0f %%RH", local.humidity);
        /* 趋势图：×10 整数化 */
        lv_chart_set_next_value(g_chart_temp, g_ser_temp,
                                (lv_value_precise_t)(local.temp * 10.0f));
        lv_chart_set_next_value(g_chart_humi, g_ser_humi,
                                (lv_value_precise_t)(local.humidity * 10.0f));
    }

    if (local.comfort_dirty) {
        uint8_t lvl = local.comfort_level < 5 ? local.comfort_level : 4;
        lv_label_set_text(g_label_comfort_val, COMFORT_STR[lvl]);
        lv_obj_set_style_text_color(g_label_comfort_val,
                                    lv_color_hex(COMFORT_COLORS[lvl]), 0);
        lv_label_set_text_fmt(g_label_comfort_hi, "HI: %.1f°C", local.heat_index);
    }

    if (local.pir_dirty) {
        if (local.pir_detected) {
            lv_label_set_text(g_label_pir, "有人");
            lv_obj_set_style_text_color(g_label_pir, CLR_YELLOW, 0);
        } else {
            lv_label_set_text(g_label_pir, "无人");
            lv_obj_set_style_text_color(g_label_pir, CLR_GREEN, 0);
        }
    }

    if (local.dist_dirty) {
        lv_label_set_text_fmt(g_label_dist, "%.1f cm", local.dist_cm);
        lv_chart_set_next_value(g_chart_dist, g_ser_dist,
                                (lv_value_precise_t)(local.dist_cm * 10.0f));
    }

    if (local.light_dirty) {
        lv_label_set_text_fmt(g_label_lux, "%u lux", local.lux);
        lv_chart_set_next_value(g_chart_lux, g_ser_lux,
                                (lv_value_precise_t)local.lux);
    }

    if (local.accel_dirty) {
        lv_label_set_text_fmt(g_label_accel,
            "X: %+.2fg   Y: %+.2fg   Z: %+.2fg   |a|: %.2fg",
            local.ax, local.ay, local.az, local.amag);
        lv_chart_set_next_value(g_chart_amag, g_ser_amag,
                                (lv_value_precise_t)(local.amag * 100.0f));
    }

    /* ── 告警横幅 ── */
    if (local.anomaly_dirty) {
        lv_label_set_text_fmt(g_label_alert,
            "  检测到震动/冲击  幅度 %.3fg", local.anomaly_mag);
        lv_obj_clear_flag(g_panel_alert, LV_OBJ_FLAG_HIDDEN);
        alert_ticks = ALERT_AUTO_HIDE_TICKS;
    }

    if (local.touch_dirty) {
        if (local.touch_x == -1 || local.touch_x == -2) {
            /* IR 遥控切换 Tab */
            uint32_t cur = lv_tabview_get_tab_active(g_tabview);
            uint32_t next;
            if (local.touch_x == -2)   /* KEY_RIGHT */
                next = (cur + 1) % 3;
            else                        /* KEY_LEFT */
                next = (cur + 3 - 1) % 3;
            lv_tabview_set_active(g_tabview, next, LV_ANIM_ON);
        } else if (alert_ticks > 0) {
            /* 触摸提前 dismiss 告警 */
            alert_ticks = 1;
        }
    }

    if (alert_ticks > 0) {
        alert_ticks--;
        if (alert_ticks == 0)
            lv_obj_add_flag(g_panel_alert, LV_OBJ_FLAG_HIDDEN);
    }

    /* ── 设置 Tab：定期刷新 MQTT / DB 状态 ── */
    if (++status_ticks >= STATUS_REFRESH_TICKS) {
        status_ticks = 0;
        lv_label_set_text(g_label_mqtt_val, mqtt_status_str());
        lv_label_set_text(g_label_db_val,   db_status_str());
    }

    return lv_timer_handler();
}
