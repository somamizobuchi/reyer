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
    vec2<float> pupil_center;
    float pupil_diameter;
};

struct GazeData {
    vec2<float> raw;
    vec2<float> filtered;
    vec2<float> velocity;
};

struct TrackerData {
    DpiData dpi;
    GazeData gaze;
    bool is_blink;
    bool is_valid;
};

struct EyeData {
    TrackerData left;
    TrackerData right;
    uint64_t timestamp;
};

struct RenderContext {
    uint32_t screen_distance_mm;
};
} // namespace reyer::core
