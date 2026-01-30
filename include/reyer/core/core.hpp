#pragma once

#include <cstdint>
#include "vec2.hpp"

namespace reyer::core {

    struct UserEvent {
        uint64_t timestamp;
        int event;
    };

    struct EyeData {
        vec2<float> p1;
        vec2<float> p4;
        vec2<float> gaze;
    };

    struct Data {
        EyeData left;
        EyeData right;
        double timestamp;
    };
}
