#include "reyer_rt/managers/graphics_manager.hpp"
#include "reyer_rt/managers/broadcast_manager.hpp"
#include "reyer_rt/net/message_types.hpp"
#include "reyer_rt/utils/utils.hpp"
#include <atomic>
#include <chrono>
#include <cstdarg>
#include <format>
#include <memory>
#include <mutex>
#include <numbers>
#include <raylib.h>
#include <spdlog/spdlog.h>
#include <thread>
#include <vector>

namespace reyer_rt::managers {

GraphicsManager::GraphicsManager(
    std::shared_ptr<BroadcastManager> &broadcast_manager,
    std::shared_ptr<PipelineManager> &pipeline_manager)
    : broadcastManager_(broadcast_manager), pipelineManager_(pipeline_manager) {
}

void GraphicsManager::Init() {
    state_.store(State::DEFAULT, std::memory_order_release);

    SetConfigFlags(FLAG_WINDOW_HIDDEN);
    SetTraceLogLevel(LOG_WARNING);
    InitWindow(0, 0, "");
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

std::future<std::error_code> GraphicsManager::SetGraphicsSettings(
    const net::message::GraphicsSettingsRequest &settings) {

    std::promise<std::error_code> promise{};
    auto future = promise.get_future();

    auto current_state = state_.load(std::memory_order_acquire);
    if (current_state != State::DEFAULT) {
        promise.set_value(
            std::make_error_code(std::errc::operation_not_permitted));
        return future;
    }

    net::message::GraphicsSettingsPromise gfx_promise{settings,
                                                      std::move(promise)};
    graphicsSettingsQueue_.push(std::move(gfx_promise));

    return future;
}

void GraphicsManager::applyGraphicsSettings_(
    net::message::GraphicsSettingsPromise &gfx_promise) {
    const auto &settings = gfx_promise.settings;
    const auto &gs = settings.graphics_settings;

    int flags = 0;
    if (gs.anti_aliasing)
        flags |= FLAG_MSAA_4X_HINT;
    if (gs.vsync)
        flags |= FLAG_VSYNC_HINT;

    SetConfigFlags(flags);
    SetTargetFPS(gs.target_fps);
    SetTraceLogCallback(&GraphicsManager::errorCallback_);

    InitWindow(640, 480, "Reyer RT");
    SetWindowMonitor(gs.monitor_index);
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
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

    graphicsSettings_ = settings;
    graphicsInitialized_ = true;
    state_.store(State::READY, std::memory_order_release);

    gfx_promise.promise.set_value(std::error_code{});

    // Broadcast GRAPHICS_READY
    if (auto bcast = broadcastManager_.lock()) {
        net::message::ProtocolEventMessage event{
            "", net::message::ProtocolEvent::GRAPHICS_READY, 0};
        bcast->Broadcast(net::message::BroadcastTopic::PROTOCOL, event);
    }

    auto mw = static_cast<uint32_t>(GetMonitorPhysicalWidth(gs.monitor_index));
    auto mh = static_cast<uint32_t>(GetMonitorPhysicalHeight(gs.monitor_index));
    renderContext_ = reyer::core::RenderContext{
        settings.view_distance_mm,
        mw,
        mh,
        reyer::core::calculatePPD(GetMonitorWidth(GetCurrentMonitor()), mw,
                                  settings.view_distance_mm),
        reyer::core::calculatePPD(GetMonitorHeight(GetCurrentMonitor()), mh,
                                  settings.view_distance_mm),
    };

    spdlog::info("Monitor: {}", GetMonitorName(gs.monitor_index));
    spdlog::info("Graphics initialized: {}x{} @ {}fps", gs.width, gs.height,
                 gs.target_fps);
    spdlog::info("Resolution: {}x{}, Physical size: {}mm x {}mm, View "
                 "distance: {}mm, PPD: {}x{}",
                 gs.width, gs.height, mw, mh, settings.view_distance_mm,
                 renderContext_.ppd_x, renderContext_.ppd_y);
}

void GraphicsManager::Run() {
    while (!stop_requested_.load(std::memory_order_acquire)) {

        switch (state_) {

        case State::DEFAULT: {
            if (auto gfx_promise = graphicsSettingsQueue_.try_pop()) {
                applyGraphicsSettings_(gfx_promise.value());
            } else {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
            break;
        }

        case State::READY: {
            pollTaskQueue_();

            bool hasTask = false;
            {
                std::lock_guard<std::mutex> lock(taskMutex_);
                hasTask = static_cast<bool>(currentTask_);
            }

            if (hasTask) {
                // Render the active task
                auto *render = currentTask_.as<reyer::plugin::IRender>();
                if (!render) {
                    spdlog::error("No valid render plugin loaded");
                    ClearCurrentTask();
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

                    auto pm = pipelineManager_.lock();
                    if (pm) {
                        if (auto calibration =
                                pm->pipeline().getCalibration()) {
                            calibration->pushCalibrationPoints(points.data(),
                                                               points.size());
                        } else {
                            spdlog::warn(
                                "No calibration plugin loaded in pipeline");
                        }
                    }
                }

                if (render->isFinished()) {
                    taskFinished_.store(true, std::memory_order_release);
                }
            } else {
                showStandbyScreen_();
            }

            if (WindowShouldClose()) {
                stop_requested_.store(true, std::memory_order_release);
            }
            break;
        }
        }
    }
    Shutdown();
}

void GraphicsManager::Shutdown() {
    {
        std::lock_guard<std::mutex> lock(taskMutex_);
        if (currentTask_) {
            currentTask_->shutdown();
            currentTask_ = reyer::plugin::Plugin();
        }
    }

    if (IsWindowReady()) {
        CloseWindow();
    }
}

void GraphicsManager::showStandbyScreen_() {
    std::optional<std::string> protocol_name;
    {
        std::lock_guard<std::mutex> lock(standbyMutex_);
        protocol_name = standbyProtocolName_;
    }

    if (protocol_name && IsKeyPressed(KEY_S)) {
        startRequested_.store(true, std::memory_order_release);
    }

    BeginDrawing();
    ClearBackground({0, 0, 0});
    if (protocol_name) {
        auto protocol_text = std::format("Protocol: {}", *protocol_name);
        auto width = MeasureText(protocol_text.c_str(), 24);
        DrawText(protocol_text.c_str(), (GetScreenWidth() - width) / 2.0f,
                 GetScreenHeight() / 2.0f, 24, WHITE);

        const char *start_prompt = "Press S to start";
        width = MeasureText(start_prompt, 30);
        DrawText(start_prompt, (GetScreenWidth() - width) / 2.0f,
                 GetScreenHeight() / 2.0f + 100.0f, 30, WHITE);
    }
    EndDrawing();
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

std::optional<net::message::GraphicsSettings>
GraphicsManager::GetCurrentGraphicsSettings() const {
    if (graphicsSettings_) {
        return graphicsSettings_->graphics_settings;
    }
    return std::nullopt;
}

reyer::core::RenderContext GraphicsManager::GetRenderContext() const {
    return renderContext_;
}

bool GraphicsManager::IsGraphicsInitialized() const {
    return graphicsInitialized_;
}

void GraphicsManager::SetCurrentTask(reyer::plugin::Plugin task) {
    std::lock_guard<std::mutex> lock(taskMutex_);
    pendingTask_ = task;
    taskFinished_.store(false, std::memory_order_release);
}

void GraphicsManager::pollTaskQueue_() {
    reyer::plugin::Plugin pending;
    {
        std::lock_guard<std::mutex> lock(taskMutex_);
        if (!pendingTask_) {
            return;
        }
        pending = std::move(pendingTask_);
    }

    if (!pending.getPath().empty()) {
        if (!ChangeDirectory(pending.getPath().parent_path().string().c_str())) {
            spdlog::warn("Failed to change directory to plugin path: {}",
                         pending.getPath().parent_path().string());
        } else {
            spdlog::info("Changed directory to: {}",
                         pending.getPath().parent_path().string());
        }
    }

    if (auto *render = pending.as<reyer::plugin::IRender>()) {
        render->setRenderContext(renderContext_);
    }

    spdlog::info("Initializing task \"{}\"", pending.getName());
    pending->init();

    std::lock_guard<std::mutex> lock(taskMutex_);
    currentTask_ = std::move(pending);
}

void GraphicsManager::ClearCurrentTask() {
    std::lock_guard<std::mutex> lock(taskMutex_);
    currentTask_ = reyer::plugin::Plugin();
    taskFinished_.store(false, std::memory_order_release);
}

bool GraphicsManager::IsCurrentTaskFinished() const {
    return taskFinished_.load(std::memory_order_acquire);
}

void GraphicsManager::SetStandbyInfo(const std::string &protocol_name) {
    std::lock_guard<std::mutex> lock(standbyMutex_);
    standbyProtocolName_ = protocol_name;
}

void GraphicsManager::ClearStandbyInfo() {
    std::lock_guard<std::mutex> lock(standbyMutex_);
    standbyProtocolName_.reset();
}

bool GraphicsManager::IsStopRequested() const {
    return stop_requested_.load(std::memory_order_acquire);
}

void GraphicsManager::RequestStop() {
    stop_requested_.store(true, std::memory_order_release);
}

bool GraphicsManager::ConsumeStartRequest() {
    return startRequested_.exchange(false, std::memory_order_acq_rel);
}

} // namespace reyer_rt::managers
