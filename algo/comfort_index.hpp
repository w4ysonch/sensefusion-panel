#pragma once

#include <cstddef>
#include <cstdint>

enum class ComfortLevel : uint8_t {
    Cold        = 0,
    Cool        = 1,
    Comfortable = 2,
    Warm        = 3,
    Hot         = 4,
};

extern "C" void algo_comfort_on_dht11(const void *payload, size_t size, void *ctx);
