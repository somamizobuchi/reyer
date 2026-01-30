cmake_minimum_required(VERSION 3.25)

project(reyer LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(CMAKE_MODULE_PATH ${PROJECT_SOURCE_DIR}/cmake)

include(ReyerOptions)
include(ReyerDependencies)

add_library(${CMAKE_PROJECT_NAME} INTERFACE)

target_include_directories(${CMAKE_PROJECT_NAME} 
    INTERFACE
    include/
)

target_link_libraries(${CMAKE_PROJECT_NAME} 
    INTERFACE
    raylib
    glaze::glaze
)

if (REYER_BUILD_APPS)
    add_subdirectory(apps)
endif ()

add_subdirectory(plugins)
