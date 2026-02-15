add_subdirectory(sample_plugin)
add_subdirectory(dummy_source)
add_subdirectory(dummy_stage)
add_subdirectory(dummy_sink)
add_subdirectory(dummy_filter)
add_subdirectory(simple_calibration)


if (CMAKE_SYSTEM_NAME STREQUAL "Linux")
    add_subdirectory(tps_calibration)
    add_subdirectory(ddpi_ipc_source)
    add_subdirectory(saccade_contingent)
    add_subdirectory(simple_filter)
endif ()
