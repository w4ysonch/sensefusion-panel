#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <pthread.h>
#include "ui_dashboard.h"
#include "lvgl/lvgl.h"
#include "../storage/db.h"
#include "../storage/settings.h"
#include "../network/mqtt_client.h"
#include "../algo/anomaly.h"

#ifdef SIMULATOR
#include "../sim/lv_drv_sdl.h"
#else
#include <fcntl.h>
#include <sys/ioctl.h>
#include <linux/kd.h>
#include "lvgl/drivers/display/lv_linux_fbdev.h"
#endif

#include "lvgl/src/widgets/chart/lv_chart_private.h"
LV_FONT_DECLARE(lv_font_sf_sc_14);
LV_FONT_DECLARE(lv_font_sf_sc_16);
LV_FONT_DECLARE(lv_font_sf_sc_28);

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
    bool ir_dirty;

    float    temp, humidity;
    float    ax, ay, az, amag;
    uint8_t  pir_detected;
    float    dist_cm;
    uint16_t lux;
    float    heat_index;
    uint8_t  comfort_level;
    float    anomaly_mag;
    int32_t  touch_x, touch_y;
    uint8_t  touch_pressed;
    int32_t  ir_key;
} sensor_cache_t;

static sensor_cache_t  g_cache    = {0};
static pthread_mutex_t g_mutex    = PTHREAD_MUTEX_INITIALIZER;
static app_settings_t  g_settings = {0};

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
static lv_obj_t *g_label_db_count;
static lv_obj_t *g_label_sysinfo;
static lv_obj_t *g_slider_brightness;
static lv_obj_t *g_slider_threshold;
static lv_obj_t *g_sw_unit;
static lv_obj_t *g_sw_mute;

/* 趋势 Tab — 每个图表卡片（card 对象）*/
static lv_obj_t *g_trend_cards[5];  /* 对应 temp/humi/dist/lux/amag */

/* 全屏详情层 */
static lv_obj_t          *g_detail_panel;
static lv_obj_t          *g_detail_chart;
static lv_chart_series_t *g_detail_ser;
static lv_obj_t          *g_detail_title;

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
    lv_obj_set_style_text_font(lbl, &lv_font_sf_sc_14, 0);
    lv_obj_align(lbl, LV_ALIGN_TOP_LEFT, 0, 0);
    return card;
}

static lv_obj_t *create_value_label(lv_obj_t *card, const char *text, int32_t y)
{
    lv_obj_t *lbl = lv_label_create(card);
    lv_label_set_text(lbl, text);
    lv_obj_set_style_text_color(lbl, CLR_VALUE, 0);
    lv_obj_set_style_text_font(lbl, &lv_font_sf_sc_28, 0);
    lv_obj_align(lbl, LV_ALIGN_TOP_LEFT, 0, y);
    return lbl;
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

/* 创建带标题的趋势折线图，返回外层 card 对象 */
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

    /* 标题 */
    lv_obj_t *lbl = lv_label_create(card);
    lv_label_set_text(lbl, title);
    lv_obj_set_style_text_color(lbl, CLR_TITLE, 0);
    lv_obj_set_style_text_font(lbl, &lv_font_sf_sc_14, 0);
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
    lv_obj_set_style_size(chart, 0, 0, LV_PART_INDICATOR);

    lv_chart_series_t *ser = lv_chart_add_series(
        chart, color, LV_CHART_AXIS_PRIMARY_Y);
    lv_chart_set_all_value(chart, ser, (y_min + y_max) / 2);

    if (ser_out)   *ser_out   = ser;
    if (chart_out) *chart_out = chart;

    /* 子控件不拦截点击，事件冒泡到 card */
    lv_obj_clear_flag(chart, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(lbl, LV_OBJ_FLAG_EVENT_BUBBLE);
    lv_obj_add_flag(chart, LV_OBJ_FLAG_EVENT_BUBBLE);
    return card;
}

/* LVGL v9 tabview tab bar 结构：lv_obj 容器 → lv_button → lv_label
 * label 不继承父控件 style，无公开 API 直接设置字体/颜色，只能遍历设置 */
static void set_tabview_tab_font(lv_obj_t *tabview, const lv_font_t *font)
{
    lv_obj_t *bar = lv_tabview_get_tab_bar(tabview);
    uint32_t n = lv_obj_get_child_count(bar);
    for (uint32_t i = 0; i < n; i++) {
        lv_obj_t *btn = lv_obj_get_child(bar, (int32_t)i);
        lv_obj_t *lbl = lv_obj_get_child(btn, 0);
        if (!lbl) continue;
        lv_obj_set_style_text_font(lbl, font, 0);
        /* 未选中：CLR_VALUE（亮白）；选中：CLR_TITLE（蓝色，LVGL 默认会覆盖） */
        lv_obj_set_style_text_color(lbl, CLR_VALUE, 0);
        lv_obj_set_style_text_color(lbl, CLR_TITLE, LV_STATE_CHECKED);
    }
}

/* ── 全屏详情层 ──────────────────────────────────────────── */

/* 每个趋势卡片携带的元数据，用于点击时填充详情层 */
typedef struct {
    lv_obj_t          *src_chart;  /* 小图的 chart 对象 */
    lv_chart_series_t *src_ser;
    int32_t            y_min;
    int32_t            y_max;
    lv_color_t         color;
    const char        *title;
} chart_meta_t;

/* 5 个图表的元数据，与 g_trend_cards[] 一一对应 */
static chart_meta_t g_chart_meta[5];

static void detail_close_cb(lv_event_t *e)
{
    (void)e;
    lv_obj_add_flag(g_detail_panel, LV_OBJ_FLAG_HIDDEN);
}

static void chart_card_click_cb(lv_event_t *e)
{
    chart_meta_t *meta = (chart_meta_t *)lv_event_get_user_data(e);

    /* 更新详情层标题 */
    lv_label_set_text(g_detail_title, meta->title);

    /* 同步量程与颜色 */
    lv_chart_set_range(g_detail_chart, LV_CHART_AXIS_PRIMARY_Y,
                       meta->y_min, meta->y_max);
    lv_chart_set_series_color(g_detail_chart, g_detail_ser, meta->color);

    /* 共享小图的 y_points 数组，大图只读展示 */
    lv_chart_set_series_ext_y_array(g_detail_chart, g_detail_ser,
                                    meta->src_ser->y_points);
    lv_chart_refresh(g_detail_chart);

    lv_obj_clear_flag(g_detail_panel, LV_OBJ_FLAG_HIDDEN);
}

static void build_detail_panel(lv_obj_t *scr)
{
    /* 全屏半透明遮罩 + 卡片 */
    g_detail_panel = lv_obj_create(scr);
    lv_obj_set_size(g_detail_panel, 1024, 600);
    lv_obj_set_pos(g_detail_panel, 0, 0);
    lv_obj_set_style_bg_color(g_detail_panel, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(g_detail_panel, LV_OPA_80, 0);
    lv_obj_set_style_border_width(g_detail_panel, 0, 0);
    lv_obj_set_style_radius(g_detail_panel, 0, 0);
    lv_obj_set_style_pad_all(g_detail_panel, 0, 0);
    lv_obj_clear_flag(g_detail_panel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(g_detail_panel, LV_OBJ_FLAG_HIDDEN);

    /* 内层卡片 */
    lv_obj_t *card = lv_obj_create(g_detail_panel);
    lv_obj_set_size(card, 960, 520);
    lv_obj_align(card, LV_ALIGN_CENTER, 0, 0);
    style_card(card);

    /* 标题 */
    g_detail_title = lv_label_create(card);
    lv_label_set_text(g_detail_title, "");
    lv_obj_set_style_text_color(g_detail_title, CLR_TITLE, 0);
    lv_obj_set_style_text_font(g_detail_title, &lv_font_sf_sc_16, 0);
    lv_obj_align(g_detail_title, LV_ALIGN_TOP_LEFT, 0, 0);

    /* 关闭按钮 */
    lv_obj_t *btn_close = lv_button_create(card);
    lv_obj_set_size(btn_close, 60, 32);
    lv_obj_align(btn_close, LV_ALIGN_TOP_RIGHT, 0, -4);
    lv_obj_set_style_bg_color(btn_close, CLR_BORDER, 0);
    lv_obj_set_style_bg_opa(btn_close, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(btn_close, 0, 0);
    lv_obj_set_style_radius(btn_close, 6, 0);
    lv_obj_add_event_cb(btn_close, detail_close_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *lbl_close = lv_label_create(btn_close);
    lv_label_set_text(lbl_close, "关闭");
    lv_obj_set_style_text_font(lbl_close, &lv_font_sf_sc_14, 0);
    lv_obj_set_style_text_color(lbl_close, CLR_VALUE, 0);
    lv_obj_center(lbl_close);

    /* 大图表 */
    g_detail_chart = lv_chart_create(card);
    lv_obj_set_size(g_detail_chart, 912, 440);
    lv_obj_align(g_detail_chart, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_chart_set_type(g_detail_chart, LV_CHART_TYPE_LINE);
    lv_chart_set_point_count(g_detail_chart, CHART_POINTS);
    lv_obj_set_style_bg_color(g_detail_chart, CLR_CARD, 0);
    lv_obj_set_style_bg_opa(g_detail_chart, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(g_detail_chart, CLR_BORDER, 0);
    lv_obj_set_style_border_width(g_detail_chart, 1, 0);
    lv_obj_set_style_size(g_detail_chart, 0, 0, LV_PART_INDICATOR);
    g_detail_ser = lv_chart_add_series(g_detail_chart,
                       lv_palette_main(LV_PALETTE_BLUE),
                       LV_CHART_AXIS_PRIMARY_Y);
}

/* ── 各 Tab 构建 ─────────────────────────────────────────── */

/* 总览 Tab — 沿用原有卡片布局，y 坐标相对于 tab 内容区 */
static void build_tab_overview(lv_obj_t *tab)
{
    /* 第一行（y=8，h=250）*/
    lv_obj_t *card_dht = create_card(tab, "温湿度  DHT11",  8,   8, 240, 250);
    g_label_temp     = create_value_label(card_dht, "--.-°C",    28);
    g_label_humidity = create_sub_label  (card_dht, "--.- %RH",  72);

    lv_obj_t *card_comfort = create_card(tab, "体感舒适度", 256,  8, 240, 250);
    g_label_comfort_val = create_value_label(card_comfort, "---",        28);
    g_label_comfort_hi  = create_sub_label  (card_comfort, "HI: --.-°C", 72);

    lv_obj_t *card_pir = create_card(tab, "人体感应  SR501", 504,  8, 240, 250);
    g_label_pir = create_value_label(card_pir, "---", 28);

    lv_obj_t *card_dist = create_card(tab, "距离  SR04", 752,  8, 256, 250);
    g_label_dist = create_value_label(card_dist, "-- cm", 28);

    /* 第二行（y=266，h=250）*/
    lv_obj_t *card_accel = create_card(tab, "三轴加速度  ADXL345",
                                        8, 266, 740, 250);
    g_label_accel = create_sub_label(card_accel,
        "X: --.--g   Y: --.--g   Z: --.--g   |a|: --.--g", 32);

    lv_obj_t *card_light = create_card(tab, "光照", 756, 266, 252, 250);
    g_label_lux = create_value_label(card_light, "---- lux", 28);
}

/* 趋势 Tab — 5 个 lv_chart（×10 整数化），点击可全屏查看 */
static void build_tab_trend(lv_obj_t *tab)
{
    /* 定义每个图表的参数 */
    static const struct {
        const char *title;
        int32_t x, y, w, h;
        int32_t y_min, y_max;
        lv_palette_t palette;
    } cfg[] = {
        { "温度 °C",          8,   8, 325, 260,    0,  500, LV_PALETTE_RED    },
        { "湿度 %RH",       341,   8, 325, 260,    0, 1000, LV_PALETTE_BLUE   },
        { "距离 cm",         674,  8, 334, 260,    0, 3000, LV_PALETTE_GREEN  },
        { "光照 lux",          8, 276, 496, 260,   0, 1000, LV_PALETTE_YELLOW },
        { "加速度幅值 (x100 g)", 512, 276, 496, 260, 0,  300, LV_PALETTE_PURPLE },
    };

    lv_chart_series_t **sers[] = {
        &g_ser_temp, &g_ser_humi, &g_ser_dist, &g_ser_lux, &g_ser_amag
    };
    lv_obj_t **charts[] = {
        &g_chart_temp, &g_chart_humi, &g_chart_dist, &g_chart_lux, &g_chart_amag
    };

    for (int i = 0; i < 5; i++) {
        lv_color_t color = lv_palette_main(cfg[i].palette);
        g_trend_cards[i] = create_chart(tab, cfg[i].title,
            cfg[i].x, cfg[i].y, cfg[i].w, cfg[i].h,
            cfg[i].y_min, cfg[i].y_max, color,
            sers[i], charts[i]);

        /* 元数据 */
        g_chart_meta[i] = (chart_meta_t){
            .src_chart = *charts[i],
            .src_ser   = *sers[i],
            .y_min     = cfg[i].y_min,
            .y_max     = cfg[i].y_max,
            .color     = color,
            .title     = cfg[i].title,
        };

        /* 点击卡片打开全屏详情 */
        lv_obj_add_flag(g_trend_cards[i], LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(g_trend_cards[i], chart_card_click_cb,
                            LV_EVENT_CLICKED, &g_chart_meta[i]);
    }
}

/* ── 设置控件回调（主线程，可直接调 LVGL 和 settings_save） ── */

#ifndef SIMULATOR
static void apply_brightness(uint8_t pct)
{
    /* 查找 /sys/class/backlight/*/brightness 写入 */
    static const char *path = "/sys/class/backlight/backlight/brightness";
    FILE *f = fopen(path, "w");
    if (f) { fprintf(f, "%u\n", (unsigned)(pct * 255u / 100u)); fclose(f); }
}
#endif

static void cb_brightness(lv_event_t *e)
{
    lv_obj_t *sl = lv_event_get_target(e);
    g_settings.brightness = (uint8_t)lv_slider_get_value(sl);
    settings_save(&g_settings);
#ifndef SIMULATOR
    apply_brightness(g_settings.brightness);
#endif
}

static void cb_unit(lv_event_t *e)
{
    lv_obj_t *sw = lv_event_get_target(e);
    g_settings.unit_fahrenheit = lv_obj_has_state(sw, LV_STATE_CHECKED) ? 1 : 0;
    settings_save(&g_settings);
}

static void cb_mute(lv_event_t *e)
{
    lv_obj_t *sw = lv_event_get_target(e);
    g_settings.alert_muted = lv_obj_has_state(sw, LV_STATE_CHECKED) ? 1 : 0;
    settings_save(&g_settings);
}

static void cb_threshold(lv_event_t *e)
{
    lv_obj_t *sl = lv_event_get_target(e);
    /* slider 范围 10~100，对应 0.1g~1.0g */
    float thr = (float)lv_slider_get_value(sl) / 100.0f;
    g_settings.anomaly_threshold = thr;
    algo_anomaly_set_threshold(thr);
    settings_save(&g_settings);
}

static void refresh_sysinfo(void)
{
    if (!g_label_sysinfo) return;
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
    unsigned long up_h = (unsigned long)uptime / 3600;
    unsigned long up_m = ((unsigned long)uptime % 3600) / 60;

    char buf[200];
    snprintf(buf, sizeof(buf),
        "CPU: %s\n内存: %lu MB 总 / %lu MB 可用\n运行时间: %luh %02lum",
        cpu, mem_total / 1024, mem_free / 1024, up_h, up_m);
    lv_label_set_text(g_label_sysinfo, buf);
}

static void cb_db_cleanup(lv_event_t *e)
{
    (void)e;
    db_cleanup_old(30);
    int64_t n = db_count();
    if (g_label_db_count) {
        if (n >= 0)
            lv_label_set_text_fmt(g_label_db_count, "已清理 30 天前记录  当前: %lld 条", (long long)n);
        else
            lv_label_set_text(g_label_db_count, "已清理 30 天前记录");
    }
}

/* 设置 Tab */
static void build_tab_settings(lv_obj_t *tab)
{
    /* MQTT 状态（左）+ DB 状态（右），y=8 h=130 */
    lv_obj_t *card_mqtt = create_card(tab, "MQTT", 8, 8, 496, 130);
    create_sub_label(card_mqtt, "主题前缀: " MQTT_TOPIC_PREFIX, 28);
    create_sub_label(card_mqtt, "Broker: mqtt_init() 传入地址", 50);
    g_label_mqtt_val = lv_label_create(card_mqtt);
    lv_label_set_text(g_label_mqtt_val, "---");
    lv_obj_set_style_text_color(g_label_mqtt_val, CLR_VALUE, 0);
    lv_obj_set_style_text_font(g_label_mqtt_val, &lv_font_sf_sc_14, 0);
    lv_obj_align(g_label_mqtt_val, LV_ALIGN_TOP_LEFT, 0, 72);

    lv_obj_t *card_db = create_card(tab, "SQLite 数据库", 512, 8, 496, 130);
#ifdef SIMULATOR
    create_sub_label(card_db, "路径: ./sensefusion.db", 28);
#else
    create_sub_label(card_db, "路径: /var/lib/sensefusion/data.db", 28);
#endif
    g_label_db_val = lv_label_create(card_db);
    lv_label_set_text(g_label_db_val, "---");
    lv_obj_set_style_text_color(g_label_db_val, CLR_VALUE, 0);
    lv_obj_set_style_text_font(g_label_db_val, &lv_font_sf_sc_14, 0);
    lv_obj_align(g_label_db_val, LV_ALIGN_TOP_LEFT, 0, 72);

    /* 调节卡，y=146 h=330 */
    lv_obj_t *card_ctrl = create_card(tab, "调节", 8, 146, 1008, 330);

    create_sub_label(card_ctrl, "背光亮度", 28);
    g_slider_brightness = lv_slider_create(card_ctrl);
    lv_obj_set_size(g_slider_brightness, 800, 20);
    lv_obj_align(g_slider_brightness, LV_ALIGN_TOP_LEFT, 0, 54);
    lv_slider_set_range(g_slider_brightness, 0, 100);
    lv_slider_set_value(g_slider_brightness, g_settings.brightness, LV_ANIM_OFF);
    lv_obj_add_event_cb(g_slider_brightness, cb_brightness, LV_EVENT_VALUE_CHANGED, NULL);

    create_sub_label(card_ctrl, "温度单位  °C / °F", 100);
    g_sw_unit = lv_switch_create(card_ctrl);
    lv_obj_align(g_sw_unit, LV_ALIGN_TOP_LEFT, 0, 122);
    if (g_settings.unit_fahrenheit) lv_obj_add_state(g_sw_unit, LV_STATE_CHECKED);
    lv_obj_add_event_cb(g_sw_unit, cb_unit, LV_EVENT_VALUE_CHANGED, NULL);

    lv_obj_t *lbl_mute_title = lv_label_create(card_ctrl);
    lv_label_set_text(lbl_mute_title, "异常告警静音");
    lv_obj_set_style_text_color(lbl_mute_title, CLR_SUB, 0);
    lv_obj_set_style_text_font(lbl_mute_title, &lv_font_sf_sc_14, 0);
    lv_obj_align(lbl_mute_title, LV_ALIGN_TOP_LEFT, 300, 100);
    g_sw_mute = lv_switch_create(card_ctrl);
    lv_obj_align(g_sw_mute, LV_ALIGN_TOP_LEFT, 300, 122);
    if (g_settings.alert_muted) lv_obj_add_state(g_sw_mute, LV_STATE_CHECKED);
    lv_obj_add_event_cb(g_sw_mute, cb_mute, LV_EVENT_VALUE_CHANGED, NULL);

    create_sub_label(card_ctrl, "异常检测阈值 (g)", 188);
    g_slider_threshold = lv_slider_create(card_ctrl);
    lv_obj_set_size(g_slider_threshold, 800, 20);
    lv_obj_align(g_slider_threshold, LV_ALIGN_TOP_LEFT, 0, 214);
    lv_slider_set_range(g_slider_threshold, 10, 100);
    lv_slider_set_value(g_slider_threshold,
                        (int32_t)(g_settings.anomaly_threshold * 100.0f), LV_ANIM_OFF);
    lv_obj_add_event_cb(g_slider_threshold, cb_threshold, LV_EVENT_VALUE_CHANGED, NULL);
}

static void build_tab_system(lv_obj_t *tab)
{
    /* 系统信息，y=8 h=140 */
    lv_obj_t *card_sys = create_card(tab, "系统信息", 8, 8, 1008, 140);
    g_label_sysinfo = lv_label_create(card_sys);
    lv_label_set_text(g_label_sysinfo, "加载中...");
    lv_obj_set_style_text_color(g_label_sysinfo, CLR_VALUE, 0);
    lv_obj_set_style_text_font(g_label_sysinfo, &lv_font_sf_sc_14, 0);
    lv_obj_align(g_label_sysinfo, LV_ALIGN_TOP_LEFT, 0, 26);
    lv_label_set_long_mode(g_label_sysinfo, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(g_label_sysinfo, 984);

    /* DB 清理，y=156 h=130 */
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
    lv_obj_add_event_cb(btn_clean, cb_db_cleanup, LV_EVENT_CLICKED, NULL);
    g_label_db_count = lv_label_create(card_clean);
    lv_label_set_text(g_label_db_count, "点击按钮执行清理");
    lv_obj_set_style_text_color(g_label_db_count, CLR_SUB, 0);
    lv_obj_set_style_text_font(g_label_db_count, &lv_font_sf_sc_14, 0);
    lv_obj_align(g_label_db_count, LV_ALIGN_TOP_LEFT, 160, 68);

    /* IR 遥控说明，y=294 h=64 */
    lv_obj_t *card_ir = create_card(tab, "红外遥控", 8, 294, 1008, 64);
    create_sub_label(card_ir,
        "KEY_LEFT / KEY_RIGHT  --  循环切换 Tab [总览/趋势/设置/系统]", 28);
}

/* ── 主界面构建入口 ───────────────────────────────────────── */
static void build_ui(const app_settings_t *settings)
{
    g_settings = *settings;
    lv_obj_t *scr = lv_screen_active();
    lv_obj_set_style_bg_color(scr, CLR_BG, 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);

    /* ── TabView（全屏） ── */
    g_tabview = lv_tabview_create(scr);
    lv_obj_set_size(g_tabview, 1024, 600);
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
    lv_obj_t *tab_system   = lv_tabview_add_tab(g_tabview, "  系统  ");

    set_tabview_tab_font(g_tabview, &lv_font_sf_sc_16);

    /* 各 tab 背景与内边距 */
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

    /* 全屏详情层（挂在 screen，渲染在 tabview 之上） */
    build_detail_panel(scr);

    /* ── 告警横幅：screen 直接子对象，渲染在 tabview 之上 ── */
    g_panel_alert = lv_obj_create(scr);
    lv_obj_set_pos(g_panel_alert, 8, 540);
    lv_obj_set_size(g_panel_alert, 1008, 52);
    lv_obj_set_style_bg_color(g_panel_alert, CLR_RED, 0);
    lv_obj_set_style_bg_opa(g_panel_alert, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(g_panel_alert, 0, 0);
    lv_obj_set_style_radius(g_panel_alert, 6, 0);
    lv_obj_add_flag(g_panel_alert, LV_OBJ_FLAG_HIDDEN);

    g_label_alert = lv_label_create(g_panel_alert);
    lv_label_set_text(g_label_alert, "告警");
    lv_obj_set_style_text_color(g_label_alert, CLR_VALUE, 0);
    lv_obj_set_style_text_font(g_label_alert, &lv_font_sf_sc_16, 0);
    lv_obj_align(g_label_alert, LV_ALIGN_LEFT_MID, 12, 0);
}

/* ── 板子触摸 indev（LVGL 原生交互：Tab 点击/滑动等） ── */
#ifndef SIMULATOR

static lv_indev_t *g_touch_indev;

static void touch_read_cb(lv_indev_t *indev, lv_indev_data_t *data)
{
    (void)indev;
    pthread_mutex_lock(&g_mutex);
    data->point.x = g_cache.touch_x;
    data->point.y = g_cache.touch_y;
    data->state   = g_cache.touch_pressed ? LV_INDEV_STATE_PRESSED
                                          : LV_INDEV_STATE_RELEASED;
    /* 读完即消费：LVGL 下次轮询看到 RELEASED，避免单次点击误判为长按 */
    g_cache.touch_pressed = 0;
    pthread_mutex_unlock(&g_mutex);
}

#endif

/* ── 公开接口 ────────────────────────────────────────────── */

void dashboard_init(const app_settings_t *settings)
{
#ifdef SIMULATOR
    lv_init();
    sdl_hal_init(1024, 600);
    printf("[dashboard] SDL2 模拟器初始化完成\n");
#else
    lv_init();
    lv_display_t *disp = lv_linux_fbdev_create();
    lv_linux_fbdev_set_file(disp, "/dev/fb0");

    /* 禁用 VT 息屏（consoleblank=600 会导致 10 分钟黑屏） */
    int tty_fd = open("/dev/console", O_RDWR);
    if (tty_fd >= 0) {
        ioctl(tty_fd, KDSETMODE, KD_GRAPHICS);
        close(tty_fd);
    }
    printf("[dashboard] FBDEV /dev/fb0 初始化完成\n");
#endif
    build_ui(settings);

#ifndef SIMULATOR
    /* 注册触摸 indev，让 LVGL 原生处理点击/滑动 */
    g_touch_indev = lv_indev_create();
    lv_indev_set_type(g_touch_indev, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(g_touch_indev, touch_read_cb);
    lv_indev_set_display(g_touch_indev, lv_display_get_default());
#endif
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

void dashboard_show_alert(float magnitude)
{
    pthread_mutex_lock(&g_mutex);
    g_cache.anomaly_mag   = magnitude;
    g_cache.anomaly_dirty = true;
    pthread_mutex_unlock(&g_mutex);
}

void dashboard_update_touch(int32_t x, int32_t y, uint8_t pressed)
{
    pthread_mutex_lock(&g_mutex);
    g_cache.touch_x        = x;
    g_cache.touch_y        = y;
    g_cache.touch_pressed  = pressed;
    g_cache.touch_dirty    = true;
    pthread_mutex_unlock(&g_mutex);
}

/* linux/input-event-codes.h */
#define IR_KEY_LEFT   105
#define IR_KEY_RIGHT  106

/* IR 遥控切换 Tab，运行在 embedmq 消费者线程：只写缓存 */
void dashboard_handle_ir_key(uint16_t key_code)
{
    if (key_code == IR_KEY_LEFT || key_code == IR_KEY_RIGHT) {
        pthread_mutex_lock(&g_mutex);
        g_cache.ir_key   = (int32_t)key_code;
        g_cache.ir_dirty = true;
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
    g_cache.ir_dirty      = false;
    pthread_mutex_unlock(&g_mutex);

    /* ── 总览 Tab 更新 ── */
    if (local.dht11_dirty) {
        if (g_settings.unit_fahrenheit) {
            float f = local.temp * 1.8f + 32.0f;
            lv_label_set_text_fmt(g_label_temp, "%.1f°F", f);
        } else {
            lv_label_set_text_fmt(g_label_temp, "%.1f°C", local.temp);
        }
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
    if (local.anomaly_dirty && !g_settings.alert_muted) {
        lv_label_set_text_fmt(g_label_alert,
            "  检测到震动/冲击  幅度 %.3fg", local.anomaly_mag);
        lv_obj_clear_flag(g_panel_alert, LV_OBJ_FLAG_HIDDEN);
        alert_ticks = ALERT_AUTO_HIDE_TICKS;
    }

    if (local.ir_dirty) {
        uint32_t cur  = lv_tabview_get_tab_active(g_tabview);
        uint32_t next = (local.ir_key == IR_KEY_RIGHT)
                        ? (cur + 1) % 4
                        : (cur + 4 - 1) % 4;
        lv_tabview_set_active(g_tabview, next, LV_ANIM_ON);
    }

    if (local.touch_dirty && alert_ticks > 0) {
        /* 触摸提前 dismiss 告警 */
        alert_ticks = 1;
    }

    if (alert_ticks > 0) {
        alert_ticks--;
        if (alert_ticks == 0)
            lv_obj_add_flag(g_panel_alert, LV_OBJ_FLAG_HIDDEN);
    }

    /* ── 设置 Tab：定期刷新 MQTT / DB 状态 / 系统信息 ── */
    if (++status_ticks >= STATUS_REFRESH_TICKS) {
        status_ticks = 0;
        lv_label_set_text(g_label_mqtt_val, mqtt_status_str());
        lv_label_set_text(g_label_db_val,   db_status_str());
        refresh_sysinfo();
    }

    return lv_timer_handler();
}
