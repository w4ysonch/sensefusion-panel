#include <math.h>
#include <string.h>
#include "anomaly.h"
#include "../app/app_init.h"
#include "../app/app_events.h"

/* 合加速度偏差检测：若当前值偏离滑动均值超过阈值，判定为震动/冲击。 */
#define MAGNITUDE_THRESHOLD 0.3f  /* 单位 g */
#define HISTORY_LEN         8     /* 滑动窗口长度 */

static float history[HISTORY_LEN];
static int   history_idx   = 0;
static int   history_count = 0;  /* 已填入样本数，< HISTORY_LEN 时为预热期 */

static float moving_average(void)
{
    float sum = 0.0f;
    for (int i = 0; i < HISTORY_LEN; i++) sum += history[i];
    return sum / HISTORY_LEN;
}

void algo_anomaly_on_adxl345(const void *payload, size_t size, void *ctx)
{
    (void)ctx;
    if (size < sizeof(evt_adxl345_t)) return;

    const evt_adxl345_t *raw = (const evt_adxl345_t *)payload;

    history[history_idx] = raw->magnitude;
    history_idx = (history_idx + 1) % HISTORY_LEN;
    if (history_count < HISTORY_LEN)
        history_count++;

    /* 窗口未填满时不检测，避免用零值污染均值产生误报 */
    if (history_count < HISTORY_LEN)
        return;

    float avg = moving_average();
    float dev = fabsf(raw->magnitude - avg);

    if (dev > MAGNITUDE_THRESHOLD) {
        evt_anomaly_t ev;
        ev.type      = 1;    /* 震动/冲击 */
        ev.magnitude = dev;
        embedmq_post(g_mq, EVT_ALERT_ANOMALY, &ev, sizeof(ev));
    }
}
