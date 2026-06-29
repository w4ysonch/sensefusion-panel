#ifndef ANOMALY_H
#define ANOMALY_H

#include <stddef.h>

void algo_anomaly_on_adxl345(const void *payload, size_t size, void *ctx);
void algo_anomaly_set_threshold(float threshold);

#endif /* ANOMALY_H */
