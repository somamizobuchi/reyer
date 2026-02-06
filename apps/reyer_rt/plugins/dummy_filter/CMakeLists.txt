add_library(dummy_filter SHARED)

target_sources(dummy_filter PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/dummy_filter.cpp
)

target_link_libraries(dummy_filter PRIVATE
    reyer
)
