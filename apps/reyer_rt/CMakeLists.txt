project(reyer_rt LANGUAGES CXX)

add_executable(${PROJECT_NAME} src/main.cpp)

set(CMAKE_MODULE_PATH ${PROJECT_SOURCE_DIR}/cmake)
include(Dependencies)

target_sources(${PROJECT_NAME}
    PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src/reyer_rt/app.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/reyer_rt/managers/graphics_manager.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/reyer_rt/managers/plugin_manager.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/reyer_rt/managers/message_manager.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/reyer_rt/managers/broadcast_manager.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/reyer_rt/net/detail/socket_base.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/reyer_rt/net/reply_socket.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/reyer_rt/net/request_socket.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/reyer_rt/net/publish_socket.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/reyer_rt/net/subscribe_socket.cpp
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

add_subdirectory(tests)
