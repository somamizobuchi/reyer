add_library(dummy_source SHARED)

target_sources(dummy_source PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/dummy_source.cpp
)

target_link_libraries(dummy_source PRIVATE
    reyer
)
