include(FetchContent)
FetchContent_Declare(
    nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG v3.12.0
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(nlohmann_json)

list(APPEND CMAKE_PREFIX_PATH "/opt/ddpi")
find_package(nng REQUIRED)
find_package(ddpi-suite REQUIRED)

reyer_add_plugin(ddpi_ipc_source 
    SOURCES ddpi_ipc_source.cpp
    LINK_LIBRARIES nlohmann_json::nlohmann_json) 

target_link_libraries(ddpi_ipc_source PRIVATE ddpi-suite::client)