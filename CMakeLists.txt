cmake_minimum_required(VERSION 4.0)

project(reyer VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(CMAKE_MODULE_PATH ${PROJECT_SOURCE_DIR}/cmake)

include(GNUInstallDirs)
include(ReyerOptions)
include(ReyerDependencies)
include(ReyerPlugin)

add_library(${CMAKE_PROJECT_NAME} INTERFACE)
add_library(reyer::reyer ALIAS ${CMAKE_PROJECT_NAME})

target_include_directories(${CMAKE_PROJECT_NAME}
    INTERFACE
    $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

target_compile_features(${CMAKE_PROJECT_NAME} INTERFACE cxx_std_23)

target_link_libraries(${CMAKE_PROJECT_NAME}
    INTERFACE
    raylib
    glaze::glaze
    HDF5::HDF5
)

include(ReyerInstall)

if(REYER_BUILD_APPS)
    add_subdirectory(apps)
endif()
