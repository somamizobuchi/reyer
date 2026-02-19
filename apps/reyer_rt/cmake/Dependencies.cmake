include(FetchContent)

# NNG
FetchContent_Declare(
    nng
    GIT_REPOSITORY https://github.com/nanomsg/nng.git
    GIT_TAG v1.11
    GIT_SHALLOW TRUE
    EXCLUDE_FROM_ALL TRUE
)
set(NNG_TESTS OFF)
set(NNG_TOOLS OFF)
set(NNG_ENABLE_NNGCAT OFF)
FetchContent_MakeAvailable(nng)

# GLAZE JSON
FetchContent_Declare(
    glaze
    GIT_REPOSITORY https://github.com/stephenberry/glaze.git
    GIT_TAG v7.0.1
    GIT_SHALLOW TRUE
    EXCLUDE_FROM_ALL TRUE
)
FetchContent_MakeAvailable(glaze)

FetchContent_Declare(
    spdlog
    GIT_REPOSITORY https://github.com/gabime/spdlog.git
    GIT_TAG v1.17.0
    GIT_SHALLOW TRUE
    EXCLUDE_FROM_ALL TRUE
)
set(SPDLOG_BUILD_EXAMPLE OFF)
set(SPDLOG_BUILD_SHARED ON)
FetchContent_MakeAvailable(spdlog)
