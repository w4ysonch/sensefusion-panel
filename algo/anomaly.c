#include <math.h>
#include <string.h>
#include "anomaly.h"
#include "../app/app_init.h"
#include "../app/app_events.h"

/* 合加速度偏差检测：若当前值偏离滑动均值超过阈值，判定为震动/冲击。 */
#define MAGNITUDE_THRESHOLD  0.3f  /* 单位 g */
#define HISTORY_LEN          8     /* 滑动窗口长度 */
#define ANOMALY_TYPE_VIBRATION 1

static float history[HISTORY_LEN];
static int   history_idx   = 0;
static int   history_count = 0;  /* 已填入样本数，< HISTORY_LEN 时为预热期 */

static float s_threshold = MAGNITUDE_THRESHOLD;

static float moving_average(void)
{
    float sum = 0.0f;
    for (int i = 0; i < HISTORY_LEN; i++) sum += history[i];
    return sum / HISTORY_LEN;
}

void algo_anomaly_set_threshold(float threshold)
{
    s_threshold = threshold;
}

void algo_anomaly_on_adxl345(const void *payload, size_t size, void *ctx)
{
    (void)ctx;
    if (size < sizeof(evt_adxl345_t)) return;

    const evt_adxl345_t *raw = (const evt_adxl345_t *)payload;

    /* 预热期：静默填充历史窗口，避免用零值污染均值产生误报 */
    if (history_count < HISTORY_LEN) {
        history[history_idx] = raw->magnitude;
        history_idx = (history_idx + 1) % HISTORY_LEN;
        history_count++;
        return;
    }

    /* 先用旧历史算基线，再与新样本比较，最后才写入新样本
     * 避免新样本自引用拉低偏差（self-reference 会缩小偏差 1/N） */
    float avg = moving_average();
    float dev = fabsf(raw->magnitude - avg);

    history[history_idx] = raw->magnitude;
    history_idx = (history_idx + 1) % HISTORY_LEN;

    if (dev > s_threshold) {
        evt_anomaly_t ev;
        ev.type      = ANOMALY_TYPE_VIBRATION;
        ev.magnitude = dev;
        embedmq_post(g_mq, EVT_ALERT_ANOMALY, &ev, sizeof(ev));
    }
}
