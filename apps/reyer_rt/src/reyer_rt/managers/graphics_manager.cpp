#include "reyer_rt/managers/graphics_manager.hpp"
#include "reyer_rt/experiment/protocol.hpp"
#include "reyer_rt/managers/broadcast_manager.hpp"
#include "reyer_rt/net/message_types.hpp"
#include "reyer_rt/utils/utils.hpp"
#include <atomic>
#include <chrono>
#include <cstdarg>
#include <future>
#include <memory>
#include <mutex>
#include <raylib.h>
#include <spdlog/spdlog.h>
#include <stdexcept>
#include <system_error>
#include <thread>
#include <vector>

namespace reyer_rt::managers {

GraphicsManager::GraphicsManager(
    std::shared_ptr<PluginManager> &plugin_manager,
    std::shared_ptr<BroadcastManager> &broadcast_manager)
    : pluginManager_(plugin_manager), broadcastManager_(broadcast_manager) {}

void GraphicsManager::Init() {
    SetConfigFlags(FLAG_WINDOW_HIDDEN);
    SetTraceLogLevel(LOG_WARNING);
    InitWindow(0, 0, "Reyer");
    pollMonitors_();
    CloseWindow();
}

void GraphicsManager::errorCallback_(int err, const char *fmt, va_list args) {
    auto message = utils::vprintf_to_string(fmt, args);
    switch (err) {
    case TraceLogLevel::LOG_TRACE:
        spdlog::trace(message);
        break;
    case TraceLogLevel::LOG_DEBUG:
        spdlog::debug(message);
        break;
    case TraceLogLevel::LOG_INFO:
        spdlog::info(message);
        break;
    case TraceLogLevel::LOG_WARNING:
        spdlog::warn(message);
        break;
    case TraceLogLevel::LOG_ERROR:
    case TraceLogLevel::LOG_FATAL:
        spdlog::error(message);
        break;
    default:
        break;
    }
}

void GraphicsManager::loadProtocol_() {
    protocolUpdated_ = false;

    if (!currentProtocol_) {
        state_.store(State::IDLE, std::memory_order_release);
        currentTaskIndex_ = 0;
        return;
    }

    int flags = 0;
    if (IsWindowReady() && !requiresWindowReload) {
        if (requiresWindowReload) {
            CloseWindow();
        } else {
            SetWindowMonitor(currentProtocol_->graphics_settings.monitor_index);

            SetWindowSize(currentProtocol_->graphics_settings.width,
                          currentProtocol_->graphics_settings.height);

            if (currentProtocol_->graphics_settings.vsync) {
                SetWindowState(FLAG_VSYNC_HINT);
            } else {
                ClearWindowState(FLAG_VSYNC_HINT);
            }

            if (currentProtocol_->graphics_settings.full_screen) {
                SetWindowState(FLAG_FULLSCREEN_MODE);
            } else {
                ClearWindowState(FLAG_FULLSCREEN_MODE);
            }

            SetTargetFPS(currentProtocol_->graphics_settings.target_fps);
            SetWindowFocused();

            state_.store(State::STANDBY, std::memory_order_release);

            // Broadcast PROTOCOL_NEW event
            if (auto bcast = broadcastManager_.lock()) {
                net::message::ProtocolEventMessage event{
                    currentProtocol_->protocol_uuid,
                    net::message::ProtocolEvent::PROTOCOL_NEW,
                    0
                };
                bcast->Broadcast(net::message::BroadcastTopic::PROTOCOL, event);
            }
            return;
        }
    }

    // Window flags
    SetConfigFlags(0);
    if (currentProtocol_->graphics_settings.anti_aliasing) {
        flags |= FLAG_MSAA_4X_HINT;
    }
    if (currentProtocol_->graphics_settings.vsync) {
        flags |= FLAG_VSYNC_HINT;
    }
    if (currentProtocol_->graphics_settings.full_screen) {
        flags |= FLAG_FULLSCREEN_MODE;
    }
    SetConfigFlags(flags);

    SetTargetFPS(currentProtocol_->graphics_settings.target_fps);

    SetTraceLogCallback(&GraphicsManager::errorCallback_);

    InitWindow(currentProtocol_->graphics_settings.width,
               currentProtocol_->graphics_settings.height,
               currentProtocol_->name.c_str());

    SetWindowMonitor(currentProtocol_->graphics_settings.monitor_index);

    SetWindowFocused();

    ClearWindowState(FLAG_WINDOW_HIDDEN);

    state_.store(State::STANDBY, std::memory_order_release);

    // Broadcast PROTOCOL_NEW event
    if (auto bcast = broadcastManager_.lock()) {
        net::message::ProtocolEventMessage event{
            currentProtocol_->protocol_uuid,
            net::message::ProtocolEvent::PROTOCOL_NEW,
            0
        };
        bcast->Broadcast(net::message::BroadcastTopic::PROTOCOL, event);
    }
}

void GraphicsManager::loadTask_(const LoadCommand& command) {
    auto broadcast_manager = broadcastManager_.lock();
    if (!broadcast_manager)
        throw std::runtime_error("Failed to get broadcast manager");

    if (!currentProtocol_)
        return;

    int nextIndex = currentTaskIndex_;
    switch (command) {
        case LoadCommand::FIRST: nextIndex = 0; break;
        case LoadCommand::LAST: nextIndex = currentProtocol_->tasks.size() - 1; break;
        case LoadCommand::NEXT: nextIndex = currentTaskIndex_ + 1; break;
        case LoadCommand::PREV: nextIndex = currentTaskIndex_ == 0 ? 0 : currentTaskIndex_ - 1; break;
        case LoadCommand::FINISH: nextIndex = currentProtocol_->tasks.size(); break;
    }

    if (currentTask_) {
        // currentTask_->EndTask();
        spdlog::info("Shutting down task \"{}\"", currentTask_.getName());
        currentTask_->shutdown();

        net::message::ProtocolEventMessage event{
            currentProtocol_->protocol_uuid,
            net::message::ProtocolEvent::TASK_END,
            currentTaskIndex_
        };
        if (auto ec = broadcast_manager->Broadcast(
                net::message::BroadcastTopic::PROTOCOL, event)) {
            spdlog::warn("Failed to send broadcast message: {}", ec.message());
        }
    }

    if (nextIndex >= currentProtocol_->tasks.size()) {
        currentTask_ = reyer::plugin::Plugin();
        state_.store(State::SAVING, std::memory_order_release);
        return;
    }

    auto plg_mngr = pluginManager_.lock();
    if (!plg_mngr) {
        throw std::runtime_error("Failed to get plugin manager");
    }

    auto task = currentProtocol_->tasks[nextIndex];
    spdlog::info("Loading task \"{}\"", task.name);
    auto plugin = plg_mngr->GetPlugin(task.name);

    if (!plugin) {
        spdlog::error("Failed to load task \"{}\": {}", task.name,
                      plugin.error().message());
        currentTask_ = reyer::plugin::Plugin();
        state_.store(State::SAVING, std::memory_order_release);
        return;
    }

    // Verify it's a render plugin
    if (plugin.value()->getType() != reyer::plugin::PluginType::RENDER) {
        spdlog::error("Task \"{}\" is not a render plugin", task.name);
        currentTask_ = reyer::plugin::Plugin();
        state_.store(State::SAVING, std::memory_order_release);
        return;
    }

    currentTask_ = plugin.value();
    currentTaskIndex_ = nextIndex;
    spdlog::info("Set current task to \"{}\"", currentTask_.getName());
    spdlog::info("Configuring task \"{}\"", currentTask_.getName());
    currentTask_->setConfigStr(task.configuration.c_str());

    spdlog::info("Initializing task \"{}\"", currentTask_.getName());
    currentTask_->init();

    net::message::ProtocolEventMessage event{
        currentProtocol_->protocol_uuid,
        net::message::ProtocolEvent::TASK_START,
        currentTaskIndex_
    };
    if (auto ec = broadcast_manager->Broadcast(
            net::message::BroadcastTopic::PROTOCOL, event)) {
        spdlog::warn("Failed to send broadcast message: {}", ec.message());
    }

    state_.store(State::RUNNING);
}

void GraphicsManager::Run() {
    while (!stop_requested_.load(std::memory_order_acquire)) {
        pollCommands_();

        switch (state_) {
        case State::IDLE: {
            {
                std::lock_guard<std::mutex> lock(protocolMutex_);
                if (protocolUpdated_) {
                    loadProtocol_();
                }
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        } break;
        case State::STANDBY: {
            std::lock_guard<std::mutex> lock(protocolMutex_);
            if (protocolUpdated_) {
                loadProtocol_();
            }
        }
            showStandbyScreen_();
            break;
        case State::RUNNING: {
            auto* render = currentTask_->asRender();
            BeginDrawing();
            ClearBackground({128, 128, 128});
            render->render();
            EndDrawing();

            if (render->isFinished()) {
                EnqueueCommand(net::message::Command::NEXT);
            }
        }
            if (WindowShouldClose()) {
                stop_requested_.store(true, std::memory_order_release);
            }
            break;
        case State::SAVING: {
            spdlog::info("Saving data");
            {
                std::lock_guard<std::mutex> lock(protocolMutex_);
                currentTaskIndex_ = 0;
            }
            state_.store(State::STANDBY, std::memory_order_release);
            spdlog::info("Saving complete");
            break;
        }
        }
    }
    spdlog::info("Exited main loop");
}

void GraphicsManager::pollCommands_() {
    if (auto cmd = commandQueue_.try_pop()) {
        auto state = state_.load(std::memory_order_acquire);
        switch (cmd.value().command) {
        case net::message::Command::START:
            if (state == State::STANDBY)
                loadTask_(LoadCommand::FIRST);
            break;
        case net::message::Command::STOP:
            if (state == State::RUNNING)
                loadTask_(LoadCommand::FINISH);
            break;
        case net::message::Command::NEXT:
            if (state == State::RUNNING)
                loadTask_(LoadCommand::NEXT);
            break;
        case net::message::Command::PREVIOUS:
            if (currentTaskIndex_ > 0 && state == State::RUNNING)
                loadTask_(LoadCommand::PREV);
            break;
        case net::message::Command::RESTART:
            if (state == State::RUNNING)
                loadTask_(LoadCommand::FIRST);
            break;
        case net::message::Command::EXIT:
            if (state == State::RUNNING) {
                state_.store(State::SAVING, std::memory_order_release);
            }
            stop_requested_.store(true, std::memory_order_release);
            break;
        }
        cmd.value().promise.set_value(std::error_code{});
    }
}

void GraphicsManager::Shutdown() {
    spdlog::info("Shutting down");
    if (IsWindowReady()) {
        CloseWindow();
    }
}

void GraphicsManager::showStandbyScreen_() {
    if (IsKeyPressed(KEY_S)) {
        EnqueueCommand(net::message::Command::START);
    }
    BeginDrawing();
    ClearBackground({128, 128, 128});
    DrawFPS(0, 0);
    DrawText("REYER", 300, 300, 30, RED);
    DrawText("Press S to start", 300, 500, 30, BLACK);
    EndDrawing();

    if (WindowShouldClose()) {
        stop_requested_.store(true, std::memory_order_release);
    }
}

std::future<std::error_code>
GraphicsManager::EnqueueCommand(const net::message::Command &command) {
    std::promise<std::error_code> promise{};
    auto future = promise.get_future();
    net::message::CommandPromise cmd{command, std::move(promise)};
    commandQueue_.push(std::move(cmd));
    return future;
}

bool GraphicsManager::SetProtocol(
    const net::message::ProtocolRequest &protocol) {
    std::lock_guard<std::mutex> lock(protocolMutex_);

    if (state_.load(std::memory_order_acquire) == State::RUNNING) {
        return false;
    }

    // Anti-aliasing requires window reload
    if (currentProtocol_->graphics_settings.anti_aliasing !=
        protocol.graphics_settings.anti_aliasing) {
        requiresWindowReload = true;
    }

    currentProtocol_ = protocol;
    protocolUpdated_ = true;

    spdlog::info("Set protocol to \"{}\"", currentProtocol_->name);
    return true;
}

std::vector<net::message::MonitorInfo> GraphicsManager::GetMonitorInfo() {
    return monitors_;
}

void GraphicsManager::pollMonitors_() {
    monitors_.clear();
    auto count = GetMonitorCount();

    for (auto i = 0; i < count; i++) {
        monitors_.emplace_back(i, GetMonitorWidth(i), GetMonitorHeight(i),
                               GetMonitorPhysicalWidth(i),
                               GetMonitorPhysicalHeight(i),
                               GetMonitorRefreshRate(i), GetMonitorName(i));
    }
}

} // namespace reyer_rt::managers
