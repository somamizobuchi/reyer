add_library(sample_plugin SHARED)

target_sources(sample_plugin PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/sample_plugin.cpp
)

target_link_libraries(sample_plugin PRIVATE
    reyer
)
