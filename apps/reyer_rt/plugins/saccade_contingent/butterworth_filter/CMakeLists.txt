cmake_minimum_required(VERSION 4.0)
project(butterworth_filter LANGUAGES CUDA)

set(CMAKE_CUDA_STANDARD 17)
set(CMAKE_CUDA_STANDARD_REQUIRED ON)

find_package(CUDAToolkit REQUIRED)

add_library(butterworth_filter SHARED
    butterworth_filter.cu
    butterworth_filter.h
)

set_target_properties(butterworth_filter PROPERTIES
    CUDA_ARCHITECTURES native
    CUDA_SEPARABLE_COMPILATION ON
    VERSION 1.0.0
    SOVERSION 1
)

target_include_directories(butterworth_filter
    PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
           $<INSTALL_INTERFACE:include>
)

target_link_libraries(butterworth_filter PUBLIC CUDA::cudart)

target_compile_options(butterworth_filter PRIVATE
    $<$<COMPILE_LANGUAGE:CUDA>:
        --use_fast_math
        --expt-relaxed-constexpr
        -O3
    >
)

install(TARGETS butterworth_filter LIBRARY DESTINATION lib)
install(FILES butterworth_filter.h DESTINATION include)
