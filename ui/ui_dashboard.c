#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <pthread.h>
#include "ui_dashboard.h"
#include "lvgl/lvgl.h"

#ifdef SIMULATOR
#include "../sim/lv_drv_sdl.h"
#endif

/* ── 颜色主题 ─────────────────────────────────────────────── */
#define CLR_BG        lv_color_hex(0x0d1117)  /* 深色背景 */
#define CLR_CARD      lv_color_hex(0x161b22)  /* 卡片背景 */
#define CLR_BORDER    lv_color_hex(0x30363d)  /* 卡片边框 */
#define CLR_TITLE     lv_color_hex(0x58a6ff)  /* 蓝色标题 */
#define CLR_VALUE     lv_color_hex(0xe6edf3)  /* 主数值白 */
#define CLR_SUB       lv_color_hex(0x8b949e)  /* 次要文字灰 */
#define CLR_GREEN     lv_color_hex(0x3fb950)  /* 正常/舒适 */
#define CLR_YELLOW    lv_color_hex(0xd29922)  /* 警告/热 */
#define CLR_RED       lv_color_hex(0xf85149)  /* 危险/告警 */

/* 舒适度等级对应颜色 */
static const uint32_t COMFORT_COLORS[] = {
    0x58a6ff,  /* 冷 → 蓝 */
    0x3fb950,  /* 凉 → 绿 */
    0x3fb950,  /* 舒适 → 绿 */
    0xd29922,  /* 热 → 黄 */
    0xf85149,  /* 酷热 → 红 */
};
static const char *COMFORT_STR[] = {"冷", "凉", "舒适", "热", "酷热"};

/* ── 线程安全数据缓存 ─────────────────────────────────────── */
typedef struct {
    bool dht11_dirty;
    bool accel_dirty;
    bool pir_dirty;
    bool dist_dirty;
    bool light_dirty;
    bool comfort_dirty;
    bool anomaly_dirty;

    float    temp, humidity;
    float    ax, ay, az, amag;
    uint8_t  pir_detected;
    float    dist_cm;
    uint16_t lux;
    float    heat_index;
    uint8_t  comfort_level;
    float    anomaly_mag;
} sensor_cache_t;

static sensor_cache_t   g_cache  = {0};
static pthread_mutex_t  g_mutex  = PTHREAD_MUTEX_INITIALIZER;

/* ── LVGL 控件指针 ───────────────────────────────────────── */
static lv_obj_t *g_label_temp;
static lv_obj_t *g_label_humidity;
static lv_obj_t *g_label_comfort_val;
static lv_obj_t *g_label_comfort_hi;
static lv_obj_t *g_label_pir;
static lv_obj_t *g_label_dist;
static lv_obj_t *g_label_lux;
static lv_obj_t *g_label_accel;
static lv_obj_t *g_panel_alert;
static lv_obj_t *g_label_alert;

/* ── 工具函数：创建传感器卡片 ───────────────────────────────── */
static lv_obj_t *create_card(lv_obj_t *parent, const char *title,
                              int32_t x, int32_t y, int32_t w, int32_t h)
{
    lv_obj_t *card = lv_obj_create(parent);
    lv_obj_set_pos(card, x, y);
    lv_obj_set_size(card, w, h);
    lv_obj_set_style_bg_color(card, CLR_CARD, 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(card, CLR_BORDER, 0);
    lv_obj_set_style_border_width(card, 1, 0);
    lv_obj_set_style_radius(card, 8, 0);
    lv_obj_set_style_pad_all(card, 12, 0);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *lbl = lv_label_create(card);
    lv_label_set_text(lbl, title);
    lv_obj_set_style_text_color(lbl, CLR_TITLE, 0);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_14, 0);
    lv_obj_align(lbl, LV_ALIGN_TOP_LEFT, 0, 0);

    return card;
}

/* ── 创建大数值标签 ─────────────────────────────────────── */
static lv_obj_t *create_value_label(lv_obj_t *card, const char *init_text,
                                    int32_t y_offset)
{
    lv_obj_t *lbl = lv_label_create(card);
    lv_label_set_text(lbl, init_text);
    lv_obj_set_style_text_color(lbl, CLR_VALUE, 0);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_28, 0);
    lv_obj_align(lbl, LV_ALIGN_TOP_LEFT, 0, y_offset);
    return lbl;
}

/* ── 创建小文字标签 ─────────────────────────────────────── */
static lv_obj_t *create_sub_label(lv_obj_t *card, const char *init_text,
                                  int32_t y_offset)
{
    lv_obj_t *lbl = lv_label_create(card);
    lv_label_set_text(lbl, init_text);
    lv_obj_set_style_text_color(lbl, CLR_SUB, 0);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_14, 0);
    lv_obj_align(lbl, LV_ALIGN_TOP_LEFT, 0, y_offset);
    return lbl;
}

/* ── 界面搭建 ────────────────────────────────────────────── */
static void build_ui(void)
{
    lv_obj_t *scr = lv_screen_active();
    lv_obj_set_style_bg_color(scr, CLR_BG, 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);

    /* ── 顶部标题栏 ── */
    lv_obj_t *header = lv_obj_create(scr);
    lv_obj_set_pos(header, 0, 0);
    lv_obj_set_size(header, 800, 44);
    lv_obj_set_style_bg_color(header, CLR_CARD, 0);
    lv_obj_set_style_bg_opa(header, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(header, 0, 0);
    lv_obj_set_style_radius(header, 0, 0);
    lv_obj_clear_flag(header, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *title = lv_label_create(header);
    lv_label_set_text(title, "感知融合面板  SenseFusion Panel");
    lv_obj_set_style_text_color(title, CLR_TITLE, 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_16, 0);
    lv_obj_align(title, LV_ALIGN_LEFT_MID, 12, 0);

    /* ── 第一行卡片（y=54, 各 185x160） ── */
    /* DHT11 温湿度 */
    lv_obj_t *card_dht = create_card(scr, "温湿度  DHT11", 8, 54, 185, 160);
    g_label_temp     = create_value_label(card_dht, "--.-°C", 24);
    g_label_humidity = create_sub_label(card_dht,   "--.- %RH", 62);

    /* 舒适度 */
    lv_obj_t *card_comfort = create_card(scr, "体感舒适度", 201, 54, 185, 160);
    g_label_comfort_val = create_value_label(card_comfort, "---", 24);
    g_label_comfort_hi  = create_sub_label(card_comfort,   "HI: --.-°C", 62);

    /* 人体感应 */
    lv_obj_t *card_pir = create_card(scr, "人体感应  SR501", 394, 54, 185, 160);
    g_label_pir = create_value_label(card_pir, "---", 24);

    /* 超声波距离 */
    lv_obj_t *card_dist = create_card(scr, "距离  SR04", 587, 54, 205, 160);
    g_label_dist = create_value_label(card_dist, "-- cm", 24);

    /* ── 第二行卡片（y=224） ── */
    /* 加速度（宽卡片） */
    lv_obj_t *card_accel = create_card(scr, "三轴加速度  ADXL345", 8, 224, 570, 160);
    g_label_accel = create_sub_label(card_accel,
        "X: --.--g   Y: --.--g   Z: --.--g   |a|: --.--g", 28);

    /* 光照 */
    lv_obj_t *card_light = create_card(scr, "光照", 586, 224, 206, 160);
    g_label_lux = create_value_label(card_light, "---- lux", 24);

    /* ── 底部告警横幅（默认隐藏） ── */
    g_panel_alert = lv_obj_create(scr);
    lv_obj_set_pos(g_panel_alert, 8, 394);
    lv_obj_set_size(g_panel_alert, 784, 50);
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

/* ── 公开接口实现 ────────────────────────────────────────── */

void dashboard_init(void)
{
#ifdef SIMULATOR
    lv_init();
    sdl_hal_init(800, 480);
    printf("[dashboard] SDL2 模拟器初始化完成\n");
#else
    /* TODO: 板子上接 framebuffer HAL */
    lv_init();
    printf("[dashboard] 板子 HAL TODO\n");
#endif
    build_ui();
}

/* 以下 update 函数运行在 embedmq 消费者线程：只写缓存，不碰 LVGL */

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
    g_cache.heat_index     = heat_index;
    g_cache.comfort_level  = (uint8_t)level;
    g_cache.comfort_dirty  = true;
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

void dashboard_handle_ir_key(uint16_t key_code)
{
    /* TODO: 红外按键控制页面切换，待 UI 扩展后实现 */
    printf("[dashboard] 遥控按键=0x%04x\n", key_code);
}

/* ── tick：主线程调用，将缓存刷入 LVGL 控件，再驱动渲染 ── */
void dashboard_tick(void)
{
    /* 拷贝一份脏数据出来，尽量缩短持锁时间 */
    sensor_cache_t local;
    pthread_mutex_lock(&g_mutex);
    local    = g_cache;
    /* 清掉所有 dirty flag */
    g_cache.dht11_dirty   = false;
    g_cache.accel_dirty   = false;
    g_cache.pir_dirty     = false;
    g_cache.dist_dirty    = false;
    g_cache.light_dirty   = false;
    g_cache.comfort_dirty = false;
    g_cache.anomaly_dirty = false;
    pthread_mutex_unlock(&g_mutex);

    /* 只有脏数据才更新控件，减少不必要的重绘 */
    if (local.dht11_dirty) {
        lv_label_set_text_fmt(g_label_temp,     "%.1f°C", local.temp);
        lv_label_set_text_fmt(g_label_humidity, "%.0f %%RH", local.humidity);
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

    if (local.dist_dirty)
        lv_label_set_text_fmt(g_label_dist, "%.1f cm", local.dist_cm);

    if (local.light_dirty)
        lv_label_set_text_fmt(g_label_lux, "%u lux", local.lux);

    if (local.accel_dirty)
        lv_label_set_text_fmt(g_label_accel,
            "X: %+.2fg   Y: %+.2fg   Z: %+.2fg   |a|: %.2fg",
            local.ax, local.ay, local.az, local.amag);

    if (local.anomaly_dirty) {
        lv_label_set_text_fmt(g_label_alert,
            "⚠ 检测到震动/冲击  幅度 %.3fg", local.anomaly_mag);
        lv_obj_clear_flag(g_panel_alert, LV_OBJ_FLAG_HIDDEN);
    }

    lv_timer_handler();
}
