find_package(CUDAToolkit REQUIRED)

reyer_add_plugin(saccade_contingent
    SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/saccade_contingent.cpp
    LINK_LIBRARIES butterworth_filter CUDA::cufft
)

add_subdirectory(butterworth_filter)
