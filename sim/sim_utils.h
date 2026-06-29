#ifndef SIM_UTILS_H
#define SIM_UTILS_H

#ifdef SIMULATOR

#include <stdlib.h>

static inline float sim_walk(float val, float min, float max, float step,
                              unsigned int *seed)
{
    float r = (float)rand_r(seed) / (float)RAND_MAX;
    float delta = (r * 2.0f - 1.0f) * step;
    val += delta;
    if (val < min) val = min;
    if (val > max) val = max;
    return val;
}

#endif /* SIMULATOR */

#endif /* SIM_UTILS_H */
