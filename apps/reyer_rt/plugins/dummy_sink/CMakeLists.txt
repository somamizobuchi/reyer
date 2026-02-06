add_library(dummy_sink SHARED)

target_sources(dummy_sink PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/dummy_sink.cpp
)

target_link_libraries(dummy_sink PRIVATE
    reyer
)
