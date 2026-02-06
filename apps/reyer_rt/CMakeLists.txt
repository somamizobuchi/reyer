project(reyer_rt LANGUAGES CXX)

add_executable(${PROJECT_NAME} src/main.cpp)

set(CMAKE_MODULE_PATH ${PROJECT_SOURCE_DIR}/cmake)
include(Dependencies)

target_sources(${PROJECT_NAME}
    PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src/app.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/managers/graphics_manager.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/managers/plugin_manager.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/managers/message_manager.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/managers/broadcast_manager.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/net/detail/socket_base.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/net/reply_socket.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/net/request_socket.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/net/publish_socket.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/net/subscribe_socket.cpp
)

target_link_libraries(${PROJECT_NAME} 
    PRIVATE
    reyer
    nng
    glaze::glaze
    spdlog::spdlog
)

target_include_directories(${PROJECT_NAME}
    PRIVATE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include/>
)

target_link_options(${PROJECT_NAME} PRIVATE -rdynamic)

add_subdirectory(plugins)
add_subdirectory(tests)
