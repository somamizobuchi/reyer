#pragma once

#include "vec2.hpp"
#include <cstdint>

namespace reyer::core {

struct UserEvent {
    uint64_t timestamp;
    int event;
};

struct DpiData {
    vec2<float> p1;
    vec2<float> p4;
    vec2<float> gaze;
};

struct EyeData {
    DpiData left;
    DpiData right;
    uint64_t timestamp;
};

struct RenderContext {
    uint32_t screen_distance_mm;
};
} // namespace reyer::core
