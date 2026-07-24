#pragma once

#include <cstddef>
#include <atomic>

extern "C" void algo_anomaly_on_adxl345(const void *payload, size_t size, void *ctx);
void algo_anomaly_set_threshold(float threshold);
