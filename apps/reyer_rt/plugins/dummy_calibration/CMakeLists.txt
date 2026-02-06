add_library(dummy_calibration SHARED)

target_sources(dummy_calibration PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/dummy_calibration.cpp
)

target_link_libraries(dummy_calibration PRIVATE
    reyer
)
