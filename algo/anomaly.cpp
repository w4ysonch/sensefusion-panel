#include "anomaly.hpp"
#include "../common/app_common.h"
#include "../storage/settings.h"
#include <cmath>
#include <atomic>

#define HISTORY_LEN            8
#define ANOMALY_TYPE_VIBRATION 1

static float history[HISTORY_LEN];
static int   history_idx   = 0;
static int   history_count = 0;

static std::atomic<float> s_threshold{SETTINGS_DEFAULT_THRESHOLD};

static float moving_average()
{
    float sum = 0.0f;
    for (int i = 0; i < HISTORY_LEN; i++) sum += history[i];
    return sum / HISTORY_LEN;
}

void algo_anomaly_set_threshold(float threshold)
{
    s_threshold.store(threshold, std::memory_order_relaxed);
}

extern "C" void algo_anomaly_on_adxl345(const void *payload, size_t size, void *ctx)
{
    (void)ctx;
    if (size < sizeof(evt_adxl345_t)) return;

    const auto *raw = static_cast<const evt_adxl345_t *>(payload);

    if (history_count < HISTORY_LEN) {
        history[history_idx] = raw->magnitude;
        history_idx = (history_idx + 1) % HISTORY_LEN;
        history_count++;
        /* 第一个样本填满整个 history，避免零值拉低均值导致冷启动假阳性 */
        if (history_count == 1) {
            for (int i = 1; i < HISTORY_LEN; i++)
                history[i] = raw->magnitude;
        }
        return;
    }

    float avg = moving_average();
    float dev = fabsf(raw->magnitude - avg);

    history[history_idx] = raw->magnitude;
    history_idx = (history_idx + 1) % HISTORY_LEN;

    float thr = s_threshold.load(std::memory_order_relaxed);
    if (dev > thr) {
        evt_anomaly_t ev{};
        ev.type      = ANOMALY_TYPE_VIBRATION;
        ev.magnitude = dev;
        embedmq_post(g_mq, EVT_ALERT_ANOMALY, &ev, sizeof(ev));
    }
}
