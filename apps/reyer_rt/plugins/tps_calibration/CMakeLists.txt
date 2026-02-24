include(FetchContent)
FetchContent_Declare(alglib
    GIT_REPOSITORY git@gitlab.com:aplabUR/alglib.git
    GIT_TAG main
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(alglib)

reyer_add_plugin(tps_calibration
    SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/tps_calibration.cpp
    LINK_LIBRARIES alglib::alglib spdlog::spdlog raylib
)

target_include_directories(tps_calibration PRIVATE include/)
