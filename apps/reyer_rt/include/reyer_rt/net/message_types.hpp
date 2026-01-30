#pragma once
#include <cstdint>
#include <future>
#include <sys/types.h>
#include <system_error>
#include <vector>
#include "reyer_rt/experiment/protocol.hpp"

namespace reyer_rt::net::message {

struct Ping {
    uint64_t timestamp{};
};

struct Pong {
    uint64_t timestamp{};
};

struct PluginInfo {
    std::string name{};
    std::string configuration_schema{};
};


enum class Command : uint8_t {
    START,
    STOP,
    NEXT,
    PREVIOUS,
    RESTART,
    EXIT
};

struct CommandRequest {
    std::string origin;
    std::string destination;
    Command command;
};

struct CommandPromise {
    Command command;
    std::promise<std::error_code> promise;
};

struct GraphicsSettings {
    int monitor_index{0};
    bool vsync{true};
    bool full_screen{false};
    bool anti_aliasing{false};
    int target_fps{60};
    int width{1920};
    int height{1080};
};

struct ProtocolRequest {
    std::string name{};
    std::string participant_id{};
    std::string notes{};
    std::vector<experiment::Task> tasks;
    GraphicsSettings graphics_settings{};
    uint32_t view_distance_mm;
    std::string protocol_uuid{};
};

struct Response {
    bool success{};
    int error_code{0};
    std::string error_message;
    std::string payload;
};


enum class ResourceCode : uint32_t {
    MONITORS = 0,
    PLUGINS,
};

struct ResourceRequest {
    ResourceCode resource_code;
};

struct MonitorInfo {
    int index;
    int width_px;
    int height_px;
    int width_mm;
    int height_mm;
    int refresh_rate;
    std::string name;
};

enum class BroadcastTopic : uint8_t {
    LOG = 0,
    PROTOCOL,   
};

enum class ProtocolEvent : uint8_t {
    PROTOCOL_NEW = 0,
    TASK_START,
    TASK_END,
};

struct ProtocolEventMessage {
    std::string protocol_uuid;
    ProtocolEvent event;   
    uint64_t data;
};

struct BroadcastMessage {
    BroadcastTopic topic;
    std::string payload;
};

} // namespace reyer_rt::net::message
