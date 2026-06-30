#ifndef APP_COMMON_H
#define APP_COMMON_H

#include <stdint.h>
#include "embedmq.h"

/* ── 全局 embedmq 实例 ───────────────────────────────────────────
 * sensor_daemon.c / ui_app.c 各自定义；其余模块通过此 extern 引用。
 * 两个进程各有独立实例，进程内存隔离，互不干扰。              */
extern embedmq_t *g_mq;

/* ── 事件名宏 ────────────────────────────────────────────────────
 * 所有 embedmq 事件字符串统一在此定义，禁止在别处硬编码。     */

/* 传感器原始数据 */
#define EVT_SENSOR_DHT11     "sensor.dht11"
#define EVT_SENSOR_ADXL345   "sensor.adxl345"
#define EVT_SENSOR_SR501     "sensor.sr501"
#define EVT_SENSOR_SR04      "sensor.sr04"
#define EVT_SENSOR_LIGHT     "sensor.light"

/* 输入事件 */
#define EVT_INPUT_TOUCH      "input.touch"
#define EVT_INPUT_IR         "input.ir"

/* 算法输出 */
#define EVT_ALGO_COMFORT     "algo.comfort"
#define EVT_ALERT_ANOMALY    "alert.anomaly"

/* ── Payload 结构体 ──────────────────────────────────────────── */

typedef struct {
    float temperature;   /* 摄氏度 */
    float humidity;      /* 相对湿度 % */
} evt_dht11_t;

typedef struct {
    float x;
    float y;
    float z;
    float magnitude;     /* |a| = sqrt(x²+y²+z²)，单位 g */
} evt_adxl345_t;

typedef struct {
    uint8_t detected;    /* 1=检测到人体 0=无 */
} evt_sr501_t;

typedef struct {
    float distance_cm;
} evt_sr04_t;

typedef struct {
    uint16_t lux;
} evt_light_t;

typedef struct {
    int32_t x;
    int32_t y;
    uint8_t pressed;
} evt_touch_t;

typedef struct {
    uint16_t key_code;   /* linux/input.h KEY_* */
} evt_ir_t;

typedef struct {
    float   heat_index;  /* 体感温度 °C */
    uint8_t level;       /* 0=冷 1=凉 2=舒适 3=热 4=酷热 */
} evt_comfort_t;

typedef struct {
    uint8_t type;        /* 异常类型：1=震动/冲击 */
    float   magnitude;   /* 偏差幅度（g） */
} evt_anomaly_t;

#endif /* APP_COMMON_H */
