#include "reyer_rt/managers/graphics_manager.hpp"
#include "reyer_rt/experiment/protocol.hpp"
#include "reyer_rt/managers/broadcast_manager.hpp"
#include "reyer_rt/net/message_types.hpp"
#include "reyer_rt/utils/utils.hpp"
#include <atomic>
#include <chrono>
#include <cstdarg>
#include <format>
#include <future>
#include <memory>
#include <mutex>
#include <numbers>
#include <raylib.h>
#include <spdlog/spdlog.h>
#include <stdexcept>
#include <system_error>
#include <thread>
#include <vector>

namespace reyer_rt::managers {

GraphicsManager::GraphicsManager(
    std::shared_ptr<PluginManager> &plugin_manager,
    std::shared_ptr<BroadcastManager> &broadcast_manager,
    std::shared_ptr<PipelineManager> &pipeline_manager)
    : pluginManager_(plugin_manager), broadcastManager_(broadcast_manager),
      pipelineManager_(pipeline_manager) {}

void GraphicsManager::Init() {
    state_.store(State::DEFAULT, std::memory_order_release);

    // Keep temporary window creation - required for pollMonitors_()
    // Raylib needs a window context to query monitor information
    SetConfigFlags(FLAG_WINDOW_HIDDEN);
    SetTraceLogLevel(LOG_WARNING);
    InitWindow(0, 0, "");
    pollMonitors_();
    CloseWindow(); // Close immediately - real window created in
                   // applyGraphicsSettings_()
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

std::future<std::error_code> GraphicsManager::SetGraphicsSettings(
    const net::message::GraphicsSettingsRequest &settings) {

    // Create promise and get future
    std::promise<std::error_code> promise{};
    auto future = promise.get_future();

    // Check if already initialized
    auto current_state = state_.load(std::memory_order_acquire);
    if (current_state != State::DEFAULT) {
        promise.set_value(
            std::make_error_code(std::errc::operation_not_permitted));
        return future;
    }

    // Enqueue settings with promise - will be applied in Run() on main thread
    net::message::GraphicsSettingsPromise gfx_promise{settings,
                                                      std::move(promise)};
    graphicsSettingsQueue_.push(std::move(gfx_promise));

    return future;
}

void GraphicsManager::applyGraphicsSettings_(
    net::message::GraphicsSettingsPromise &gfx_promise) {
    const auto &settings = gfx_promise.settings;
    const auto &gs = settings.graphics_settings;

    // Set config flags (fullscreen is toggled after monitor selection)
    int flags = 0;
    if (gs.anti_aliasing)
        flags |= FLAG_MSAA_4X_HINT;
    if (gs.vsync)
        flags |= FLAG_VSYNC_HINT;

    SetConfigFlags(flags);
    SetTargetFPS(gs.target_fps);
    SetTraceLogCallback(&GraphicsManager::errorCallback_);

    // Create window on default monitor with small size first
    InitWindow(640, 480, "Reyer RT");
    SetWindowMonitor(gs.monitor_index);
    std::this_thread::sleep_for(std::chrono::milliseconds(
        300)); // Wait for monitor change to take effect
    ClearWindowState(FLAG_WINDOW_HIDDEN);
    SetWindowSize(gs.width, gs.height);
    if (gs.full_screen && !IsWindowFullscreen()) {
        ToggleFullscreen();
    }
    SetWindowFocused();
    auto render_width = GetRenderWidth();
    auto render_height = GetRenderHeight();
    spdlog::info("Selected monitor {} with resolution {}x{}", gs.monitor_index,
                 render_width, render_height);

    // Store settings for later access
    graphicsSettings_ = settings;
    graphicsInitialized_ = true;
    state_.store(State::STANDBY, std::memory_order_release);

    // Set promise to signal completion
    gfx_promise.promise.set_value(std::error_code{});

    // Broadcast GRAPHICS_READY
    if (auto bcast = broadcastManager_.lock()) {
        net::message::ProtocolEventMessage event{
            "", // No protocol UUID yet
            net::message::ProtocolEvent::GRAPHICS_READY, 0};
        bcast->Broadcast(net::message::BroadcastTopic::PROTOCOL, event);
    }

    auto mw = static_cast<uint32_t>(GetMonitorPhysicalWidth(gs.monitor_index));
    auto mh = static_cast<uint32_t>(GetMonitorPhysicalHeight(gs.monitor_index));
    // Store render context
    renderContext_ = reyer::core::RenderContext{
        settings.view_distance_mm,
        mw,
        mh,
        reyer::core::calculatePPD(GetScreenWidth(), mw,
                                  settings.view_distance_mm), // PPD vertical
        reyer::core::calculatePPD(GetScreenHeight(), mh,
                                  settings.view_distance_mm), // PPD horizontal
    };

    spdlog::info("Monitor: {}", GetMonitorName(gs.monitor_index));
    spdlog::info("Graphics initialized: {}x{} @ {}fps", gs.width, gs.height,
                 gs.target_fps);
    spdlog::info("Resolution: {}x{}, Physical size: {}mm x {}mm, View "
                 "distance: {}mm, PPD: {}x{}",
                 gs.width, gs.height, mw, mh, settings.view_distance_mm,
                 renderContext_.ppd_x, renderContext_.ppd_y);
}

void GraphicsManager::loadProtocol_() {
    protocolUpdated_ = false;

    if (!currentProtocol_) {
        state_.store(State::STANDBY, std::memory_order_release);
        currentTaskIndex_ = 0;
        return;
    }

    // Graphics MUST be initialized
    if (!graphicsInitialized_) {
        spdlog::error("Cannot load protocol: graphics not initialized");
        state_.store(State::DEFAULT, std::memory_order_release);
        return;
    }

    // Just transition to STANDBY
    state_.store(State::STANDBY, std::memory_order_release);

    // Create HDF5 file for protocol
    auto filename = std::format("/tmp/{}.h5", currentProtocol_->protocol_uuid);
    currentFile_ = std::make_shared<reyer::h5::File>(filename, H5F_ACC_TRUNC);

    // Broadcast PROTOCOL_NEW
    if (auto bcast = broadcastManager_.lock()) {
        net::message::ProtocolEventMessage event{
            currentProtocol_->protocol_uuid,
            net::message::ProtocolEvent::PROTOCOL_NEW, 0};
        bcast->Broadcast(net::message::BroadcastTopic::PROTOCOL, event);
    }
}

void GraphicsManager::loadTask_(const LoadCommand &command) {
    auto broadcast_manager = broadcastManager_.lock();
    if (!broadcast_manager)
        throw std::runtime_error("Failed to get broadcast manager");

    if (!currentProtocol_)
        return;

    int nextIndex = currentTaskIndex_;
    switch (command) {
    case LoadCommand::FIRST:
        nextIndex = 0;
        break;
    case LoadCommand::LAST:
        nextIndex = currentProtocol_->tasks.size() - 1;
        break;
    case LoadCommand::NEXT:
        nextIndex = currentTaskIndex_ + 1;
        break;
    case LoadCommand::PREV:
        nextIndex = currentTaskIndex_ == 0 ? 0 : currentTaskIndex_ - 1;
        break;
    case LoadCommand::FINISH:
        nextIndex = currentProtocol_->tasks.size();
        break;
    }

    if (currentTask_) {
        // Stop eye data writer before shutting down the task
        if (eyeDataWriter_) {
            eyeDataWriter_->Stop();
            eyeDataWriter_.reset();
        }
        currentGroup_.reset();

        spdlog::info("Shutting down task \"{}\"", currentTask_.getName());
        currentTask_->reset();
        currentTask_->shutdown();

        net::message::ProtocolEventMessage event{
            currentProtocol_->protocol_uuid,
            net::message::ProtocolEvent::TASK_END, currentTaskIndex_};
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
    if (!plugin.value().as<reyer::plugin::IRender>()) {
        spdlog::error("Task \"{}\" is not a render plugin", task.name);
        currentTask_ = reyer::plugin::Plugin();
        state_.store(State::SAVING, std::memory_order_release);
        return;
    }

    currentTask_ = plugin.value();
    currentTaskIndex_ = nextIndex;
    spdlog::info("Set current task to \"{}\"", currentTask_.getName());
    spdlog::info("Configuring task \"{}\"", currentTask_.getName());
    if (auto *configurable = currentTask_.as<reyer::plugin::IConfigurable>()) {
        configurable->setConfigStr(task.configuration.c_str());
    }

    spdlog::info("Initializing task \"{}\"", currentTask_.getName());
    if (auto *render = currentTask_.as<reyer::plugin::IRender>()) {
        render->setRenderContext(renderContext_);
    }
    currentTask_->init();

    // Set current task as pipeline sink and create HDF5 writer
    if (auto pipeline_mgr = pipelineManager_.lock()) {
        pipeline_mgr->ReplaceSink(currentTask_);

        if (currentFile_) {
            auto group_name = std::format("task_{:03d}", currentTaskIndex_);
            currentGroup_ = std::make_unique<reyer::h5::Group>(
                currentFile_->get(), group_name);
            eyeDataWriter_ = std::make_unique<stages::EyeDataWriter>(
                currentGroup_->get());
            pipeline_mgr->AddSink(eyeDataWriter_.get());
            eyeDataWriter_->Spawn();
        }
    }

    net::message::ProtocolEventMessage event{
        currentProtocol_->protocol_uuid,
        net::message::ProtocolEvent::TASK_START, currentTaskIndex_};
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

        case State::DEFAULT: {
            // Check if graphics settings have been queued
            if (auto gfx_promise = graphicsSettingsQueue_.try_pop()) {
                // Apply settings on main thread
                applyGraphicsSettings_(gfx_promise.value());
            } else {
                // Wait for graphics settings
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
            break;
        }

        case State::STANDBY: {
            {
                std::lock_guard<std::mutex> lock(protocolMutex_);
                if (protocolUpdated_) {
                    loadProtocol_();
                }
            }
            showStandbyScreen_();
            break;
        }

        case State::RUNNING: {
            auto *render = currentTask_.as<reyer::plugin::IRender>();
            if (!render) {
                spdlog::error("No valid render plugin loaded");
                state_.store(State::SAVING, std::memory_order_release);
                break;
            }
            BeginDrawing();
            ClearBackground({128, 128, 128});
            render->render();
            EndDrawing();

            if (render->getCalibrationPointCount() > 0) {
                std::vector<reyer::plugin::CalibrationPoint> points(
                    render->getCalibrationPointCount());
                render->getCalibrationPoints(points.data());

                auto calibration =
                    pipelineManager_.lock()->pipeline().getCalibration();
                calibration->pushCalibrationPoints(points.data(),
                                                   points.size());
            }

            if (render->isFinished()) {
                EnqueueCommand(net::message::Command::NEXT);
            }
            if (WindowShouldClose()) {
                stop_requested_.store(true, std::memory_order_release);
            }
            break;
        }

        case State::SAVING: {
            spdlog::info("Saving data");
            if (auto pipeline_mgr = pipelineManager_.lock()) {
                pipeline_mgr->RemoveSink();
            }
            if (eyeDataWriter_) {
                eyeDataWriter_->Stop();
                eyeDataWriter_.reset();
            }
            currentGroup_.reset();
            currentFile_.reset();
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
    Shutdown();
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
    if (eyeDataWriter_) {
        eyeDataWriter_->Stop();
        eyeDataWriter_.reset();
    }
    currentGroup_.reset();
    currentFile_.reset();

    // Shut down and release the plugin BEFORE closing the window
    // Plugin may have OpenGL resources that need a valid context to destroy
    if (currentTask_) {
        currentTask_->shutdown();
        currentTask_ = reyer::plugin::Plugin();
    }

    if (IsWindowReady()) {
        CloseWindow();
    }
}

void GraphicsManager::showStandbyScreen_() {
    std::optional<net::message::ProtocolRequest> protocol;
    {
        std::lock_guard<std::mutex> lock(protocolMutex_);
        protocol = currentProtocol_;
    }

    if (protocol && IsKeyPressed(KEY_S)) {
        EnqueueCommand(net::message::Command::START);
        return;
    }

    BeginDrawing();
    ClearBackground({0, 0, 0});
    if (protocol) {
        auto protocol_text =
            std::format("Protocol: {}\n"
                        "ID: {}",
                        protocol->name, protocol->protocol_uuid);
        auto width = MeasureText(protocol_text.c_str(), 24);
        DrawText(protocol_text.c_str(), (GetScreenWidth() - width) / 2.0f,
                 GetScreenHeight() / 2.0f, 24, WHITE);

        const char *start_prompt = "Press S to start";
        width = MeasureText(start_prompt, 30);
        DrawText(start_prompt, (GetScreenWidth() - width) / 2.0f,
                 GetScreenHeight() / 2.0f + 100.0f, 30, WHITE);
    }
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

net::message::RuntimeState GraphicsManager::GetRuntimeState() const {
    auto current_state = state_.load(std::memory_order_acquire);
    switch (current_state) {
    case State::DEFAULT:
        return net::message::RuntimeState::DEFAULT;
    case State::STANDBY:
        return net::message::RuntimeState::STANDBY;
    case State::RUNNING:
    case State::SAVING:
        return net::message::RuntimeState::RUNNING;
    default:
        return net::message::RuntimeState::DEFAULT;
    }
}

std::optional<net::message::GraphicsSettings>
GraphicsManager::GetCurrentGraphicsSettings() const {
    if (graphicsSettings_) {
        return graphicsSettings_->graphics_settings;
    }
    return std::nullopt;
}

} // namespace reyer_rt::managers
