add_library(simple_calibration SHARED)

target_sources(simple_calibration PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/simple_calibration.cpp
)

target_link_libraries(simple_calibration PRIVATE
    reyer
    raylib
    spdlog::spdlog
)
