#pragma once
#include <cstdint>
#include <string>
#include "raylib.h"

namespace reyer::plugin {

    struct SampleConfiguration {
        uint32_t n_trials;
        bool is_debug;
        std::string type;
        Color square_color;
    };

}
