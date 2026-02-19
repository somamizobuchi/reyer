include(FetchContent)

find_package(raylib REQUIRED)

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
