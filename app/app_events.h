#ifndef APP_EVENTS_H
#define APP_EVENTS_H

#include <stdint.h>

/* ---------------------------------------------------------
 * 事件名定义 — 所有 embedmq 事件字符串统一放这里。
 * 全局只用这些宏，不要在别处硬编码字符串。
 * --------------------------------------------------------- */

/* 传感器原始数据事件 */
#define EVT_SENSOR_DHT11     "sensor.dht11"
#define EVT_SENSOR_ADXL345   "sensor.adxl345"
#define EVT_SENSOR_SR501     "sensor.sr501"
#define EVT_SENSOR_SR04      "sensor.sr04"
#define EVT_SENSOR_LIGHT     "sensor.light"

/* 输入事件 */
#define EVT_INPUT_TOUCH      "input.touch"
#define EVT_INPUT_IR         "input.ir"

/* 算法输出事件 */
#define EVT_ALGO_COMFORT     "algo.comfort"
#define EVT_ALERT_ANOMALY    "alert.anomaly"

/* ---------------------------------------------------------
 * 各事件的 payload 结构体
 * --------------------------------------------------------- */

typedef struct {
    float temperature;   /* 摄氏度 */
    float humidity;      /* 相对湿度 % */
} evt_dht11_t;

typedef struct {
    float x;
    float y;
    float z;
    float magnitude;     /* 合加速度 |a| = sqrt(x²+y²+z²)，单位 g */
} evt_adxl345_t;

typedef struct {
    uint8_t detected;    /* 1=检测到人体 0=无 */
} evt_sr501_t;

typedef struct {
    float distance_cm;   /* 测距结果，单位 cm */
} evt_sr04_t;

typedef struct {
    uint16_t lux;        /* 光照强度，单位 lux（ADC 原始值换算） */
} evt_light_t;

typedef struct {
    int32_t x;
    int32_t y;
    uint8_t pressed;
} evt_touch_t;

typedef struct {
    uint16_t key_code;   /* linux/input.h 中的 KEY_* 值 */
} evt_ir_t;

typedef struct {
    float   heat_index;  /* 体感温度，单位 °C */
    uint8_t level;       /* 0=冷 1=凉 2=舒适 3=热 4=酷热 */
} evt_comfort_t;

typedef struct {
    uint8_t type;        /* 异常类型：1=震动/冲击 */
    float   magnitude;   /* 偏差幅度（g） */
} evt_anomaly_t;

#endif /* APP_EVENTS_H */
