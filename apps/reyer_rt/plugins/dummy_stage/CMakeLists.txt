add_library(dummy_stage SHARED)

target_sources(dummy_stage PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/dummy_stage.cpp
)

target_link_libraries(dummy_stage PRIVATE
    reyer
)
