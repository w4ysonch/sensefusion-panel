#ifndef COMFORT_INDEX_H
#define COMFORT_INDEX_H

#include <stddef.h>

/* 舒适度等级 */
typedef enum {
    COMFORT_COLD        = 0,  /* 冷 */
    COMFORT_COOL        = 1,  /* 凉 */
    COMFORT_COMFORTABLE = 2,  /* 舒适 */
    COMFORT_WARM        = 3,  /* 热 */
    COMFORT_HOT         = 4,  /* 酷热 */
} comfort_level_t;

/* embedmq handler：订阅 EVT_SENSOR_DHT11，计算体感指数后发 EVT_ALGO_COMFORT */
void algo_comfort_on_dht11(const void *payload, size_t size, void *ctx);

#endif /* COMFORT_INDEX_H */
