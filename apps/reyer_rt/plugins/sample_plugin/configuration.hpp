#pragma once
#include "raylib.h"
#include <cstdint>
#include <glaze/core/meta.hpp>
#include <glaze/json/schema.hpp>
#include <string>

namespace reyer::plugin {

struct SampleConfiguration {
    Color square_color{255, 0, 0, 255};
};

} // namespace reyer::plugin
