include(FetchContent)

find_package(raylib 6.0 QUIET)
if(NOT ${raylib_FOUND})
    message("Could not find raylib. Fetching using `FetchContent`")
    FetchContent_Declare(
        raylib
        GIT_REPOSITORY https://github.com/raysan5/raylib.git
        GIT_TAG 6.0
        GIT_SHALLOW TRUE
    )
    set(BUILD_EXAMPLES OFF)
    set(PLATFORM "Desktop")
    set(BUILD_SHARED_LIBS ON)
    FetchContent_MakeAvailable(raylib)
endif()

find_package(HDF5 REQUIRED)

# GLAZE JSON
FetchContent_Declare(
    glaze
    GIT_REPOSITORY https://github.com/stephenberry/glaze.git
    GIT_TAG v7.0.1
    GIT_SHALLOW TRUE
    EXCLUDE_FROM_ALL TRUE
)
FetchContent_MakeAvailable(glaze)
