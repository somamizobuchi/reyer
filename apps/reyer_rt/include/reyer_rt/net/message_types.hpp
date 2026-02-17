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
    std::string default_configuration{};
};

enum class Command : uint8_t {
    START,
    STOP,
    NEXT,
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

struct MonitorInfo {
    int index;
    int width_px;
    int height_px;
    int width_mm;
    int height_mm;
    int refresh_rate;
    std::string name;
};

struct GraphicsSettingsRequest {
    GraphicsSettings graphics_settings{};
    uint32_t view_distance_mm;
};

struct GraphicsSettingsPromise {
    GraphicsSettingsRequest settings;
    std::promise<std::error_code> promise;
};

struct ProtocolRequest {
    std::string name{};
    std::string participant_id{};
    std::string notes{};
    std::vector<experiment::Task> tasks;
    std::string protocol_uuid{};
};

struct PipelineConfigRequest {
    std::string pipeline_source{};
    std::string pipeline_calibration{};
    std::string pipeline_filter{};
    std::vector<std::string> pipeline_stages{};
};

struct Response {
    bool success{};
    int error_code{0};
    std::string error_message;
    std::string payload;
};

enum class ResourceCode : uint32_t {
    RUNTIME_STATE = 0,
    AVAILABLE_MONITORS,
    AVAILABLE_SOURCES,
    AVAILABLE_STAGES,
    AVAILABLE_SINKS,
    AVAILABLE_TASKS,
    CURRENT_GRAPHICS_SETTINGS,
    CURRENT_PROTOCOL,
    CURRENT_TASK,
    AVAILABLE_CALIBRATIONS,
    AVAILABLE_FILTERS
};

struct ResourceRequest {
    ResourceCode resource_code;
};

enum class RuntimeState : uint8_t {
    DEFAULT = 0,
    STANDBY = 1,
    RUNNING = 2,
    SAVING = 3,
};


enum class BroadcastTopic : uint8_t {
    LOG = 0,
    PROTOCOL,   
};

enum class ProtocolEvent : uint8_t {
    GRAPHICS_READY = 0,
    PROTOCOL_NEW = 1,
    TASK_START = 2,
    TASK_END = 3,
    PROTOCOL_LOADED = 4,
};

struct ProtocolEventMessage {
    std::string protocol_uuid;
    ProtocolEvent event;
    uint64_t data;
    std::string protocol_name{};
    std::string participant_id{};
    std::string notes{};
    std::vector<experiment::Task> tasks{};
    std::string file_path{};
};

struct BroadcastMessage {
    BroadcastTopic topic;
    std::string payload;
};

} // namespace reyer_rt::net::message
