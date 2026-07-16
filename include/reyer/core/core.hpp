#pragma once

#include "vec2.hpp"
#include <chrono>
#include <cstdint>

namespace reyer::core {

// The reference clock for everything reyer records, in microseconds. Sources
// stamp samples with it on receipt and the recorder stamps events with it, so
// the two align. It is raw CLOCK_MONOTONIC, shared by the app and every
// dlopen'd plugin in the process, which is what lets a plugin stamp a value the
// app can compare. Timestamps are rebased against the task's start as they are
// written, so the arbitrary epoch here never reaches the file.
inline uint64_t now_us() {
    return static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now().time_since_epoch())
            .count());
}

struct UserEvent {
    uint64_t timestamp; // microseconds, rebased to the task start
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
    // Microseconds. Set by SourceBase when the sample is received; rebased to
    // the task start when written. Use this to align against events.
    uint64_t timestamp;
    // Microseconds on the source's own clock (for DDPI, the camera's frame
    // time). Free of the IPC/queue jitter in `timestamp`, so prefer it for
    // inter-sample intervals; its epoch is the device's, so it is not
    // comparable to `timestamp` or to events. Zero if the source has none.
    uint64_t device_timestamp;
};

struct RenderContext {
    uint32_t screen_distance_mm;
    uint32_t screen_width_mm;
    uint32_t screen_height_mm;
    double ppd_x;
    double ppd_y;
};
} // namespace reyer::core
