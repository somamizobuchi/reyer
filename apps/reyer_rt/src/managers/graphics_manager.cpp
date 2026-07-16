#include "reyer_rt/managers/graphics_manager.hpp"
#include "reyer_rt/managers/broadcast_manager.hpp"
#include "reyer_rt/net/message_types.hpp"
#include "reyer_rt/utils/utils.hpp"
#include <cstdarg>
#include <format>
#include <raylib.h>
#include <spdlog/spdlog.h>
#include "pthread.h"

namespace reyer_rt::managers {

GraphicsManager::GraphicsManager(
    std::shared_ptr<BroadcastManager> &broadcast_manager,
    std::shared_ptr<PipelineManager> &pipeline_manager)
    : broadcastManager_(broadcast_manager), pipelineManager_(pipeline_manager) {
}

void GraphicsManager::Init() {
    state_.store(State::DEFAULT, std::memory_order_release);

    // Real-time priority is only held while actively rendering an
    // experiment; see setRealtimePriority_().

    // Setup raylib logging
    SetTraceLogLevel(LOG_ALL);
    SetTraceLogCallback(&GraphicsManager::errorCallback_);

    SetConfigFlags(FLAG_WINDOW_HIDDEN);
    InitWindow(800, 600, "");
    pollMonitors_();
    CloseWindow();
}

void GraphicsManager::setRealtimePriority_(bool enable) {
    if (enable == isRealtime_) {
        return;
    }

    struct sched_param param;
    param.sched_priority = enable ? 30 : 0;
    int policy = enable ? SCHED_RR : SCHED_OTHER;

    int ret = pthread_setschedparam(pthread_self(), policy, &param);
    if (ret != 0) {
        spdlog::info("Failed to set thread scheduling: {}", std::strerror(ret));
        return;
    }

    isRealtime_ = enable;
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

    if (gs.full_screen) {
        flags |= FLAG_FULLSCREEN_MODE;
    }

    SetConfigFlags(flags);
    InitWindow(gs.width, gs.height, "Reyer");

    // SetWindowState(flags);
    SetTargetFPS(gs.target_fps);

    SetWindowSize(gs.width, gs.height);

    // if (gs.full_screen && !IsWindowFullscreen()) {
    //     SetWindowState(FLAG_FULLSCREEN_MODE);
    //     // ToggleFullscreen();
    // }
    std::this_thread::sleep_for(std::chrono::milliseconds(100)); // Allow time for monitor switch
    SetWindowMonitor(gs.monitor_index);
    ClearWindowState(FLAG_WINDOW_HIDDEN);
    SetWindowFocused();

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

    spdlog::info("Monitor ({}): {}", gs.monitor_index, GetMonitorName(gs.monitor_index));
    spdlog::info("Graphics initialized: {}x{} @ {}fps", gs.width, gs.height,
                 gs.target_fps);
    spdlog::info("Resolution: {}x{}, Physical size: {}mm x {}mm, View "
                 "distance: {}mm, PPD: {:.2f}x{:.2f}",
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
            // Plugin init (and the idle/standby path below) should never
            // run at real-time priority.
            setRealtimePriority_(false);
            pollTaskQueue_();

            // Hold taskMutex_ for the whole render so the task can't be
            // retired/reassigned by another thread mid-frame.
            std::unique_lock<std::mutex> lock(taskMutex_);

            if (currentTask_) {
                // Render the active task
                auto *render = currentTask_.as<reyer::plugin::IRender>();
                if (!render) {
                    spdlog::error("No valid render plugin loaded");
                    retireTask_(std::move(currentTask_));
                    currentTask_ = reyer::plugin::Plugin();
                    taskFinished_.store(false, std::memory_order_release);
                    break;
                }

                // Real-time priority only while actually driving the
                // experiment's render loop.
                setRealtimePriority_(true);

                BeginDrawing();
                ClearBackground({128, 128, 128});
                render->render();
                EndDrawing();

                if (auto cal = render->takeCalibration()) {
                    if (auto pm = pipelineManager_.lock()) {
                        // Capture currentTask_ so the plugin .so stays loaded
                        // for the entire lifetime of the calibration object
                        auto plugin_anchor = currentTask_;
                        pm->SetCalibration(
                            std::shared_ptr<reyer::plugin::ICalibration>(
                                cal.release(),
                                [plugin_anchor = std::move(plugin_anchor)](
                                    reyer::plugin::ICalibration *p) {
                                    delete p;
                                }));
                    }
                }

                if (render->isFinished()) {
                    taskFinished_.store(true, std::memory_order_release);
                }
            } else {
                lock.unlock();
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
    setRealtimePriority_(false);
    {
        std::lock_guard<std::mutex> lock(taskMutex_);
        if (currentTask_) {
            retireTask_(std::move(currentTask_));
            currentTask_ = reyer::plugin::Plugin();
        }
        // Fulfil any teardown promises left pending so callers don't block.
        clearRequested_ = false;
        for (auto &p : clearPromises_) {
            p.set_value();
        }
        clearPromises_.clear();
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
        spdlog::info("Found monitor {}: {}x{} @ {}Hz, Physical size: {}mm x "
                     "{}mm",
                     i, GetMonitorWidth(i), GetMonitorHeight(i),
                     GetMonitorRefreshRate(i), GetMonitorPhysicalWidth(i),
                     GetMonitorPhysicalHeight(i));
    }
}

std::optional<net::message::GraphicsSettings>
GraphicsManager::GetCurrentGraphicsSettings() const {
    if (graphicsSettings_) {
        return graphicsSettings_->graphics_settings;
    }
    return std::nullopt;
}

std::optional<net::message::GraphicsSettingsRequest>
GraphicsManager::GetCurrentGraphicsSettingsRequest() const {
    return graphicsSettings_;
}

reyer::core::RenderContext GraphicsManager::GetRenderContext() const {
    return renderContext_;
}

bool GraphicsManager::IsGraphicsInitialized() const {
    return graphicsInitialized_;
}

void GraphicsManager::SetCurrentTask(reyer::plugin::Plugin task,
                                     reyer::plugin::IRecorder *recorder) {
    std::lock_guard<std::mutex> lock(taskMutex_);
    pendingTask_ = task;
    pendingRecorder_ = recorder;
    taskFinished_.store(false, std::memory_order_release);
}

void GraphicsManager::retireTask_(reyer::plugin::Plugin task) {
    // Runs on the graphics thread. reset()+shutdown() may touch Raylib/GL
    // state, so they must only ever run here.
    if (task) {
        spdlog::info("Shutting down task \"{}\"", task.getName());
        task->reset();
        task->shutdown();
    }
}

void GraphicsManager::pollTaskQueue_() {
    reyer::plugin::Plugin pending;
    reyer::plugin::Plugin outgoing;
    reyer::plugin::IRecorder *pending_recorder = nullptr;
    std::vector<std::promise<void>> promises;
    {
        std::lock_guard<std::mutex> lock(taskMutex_);

        // Retire the current task if a clear was requested. If a new task is
        // also pending it will replace the outgoing one below.
        if (clearRequested_) {
            outgoing = std::move(currentTask_);
            currentTask_ = reyer::plugin::Plugin();
            taskFinished_.store(false, std::memory_order_release);
            clearRequested_ = false;
            promises = std::move(clearPromises_);
            clearPromises_.clear();
        }

        if (pendingTask_) {
            // Replacing a live task: retire it first (still on this thread).
            if (currentTask_) {
                outgoing = std::move(currentTask_);
                currentTask_ = reyer::plugin::Plugin();
            }
            pending = std::move(pendingTask_);
            pendingTask_ = reyer::plugin::Plugin();
            pending_recorder = pendingRecorder_;
            pendingRecorder_ = nullptr;
        }
    }

    // Tear down the outgoing task before initialising the incoming one so the
    // shutdown/init ordering is preserved.
    if (outgoing) {
        retireTask_(std::move(outgoing));
    }
    for (auto &p : promises) {
        p.set_value();
    }

    if (!pending) {
        return;
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
        render->setRecorder(pending_recorder);
    }

    spdlog::info("Initializing task \"{}\"", pending.getName());
    pending->init();

    std::lock_guard<std::mutex> lock(taskMutex_);
    currentTask_ = std::move(pending);
}

std::future<void> GraphicsManager::ClearCurrentTask() {
    std::promise<void> promise;
    auto future = promise.get_future();

    std::lock_guard<std::mutex> lock(taskMutex_);

    // Nothing to tear down (and nothing pending to tear down): resolve now.
    if (!currentTask_ && !pendingTask_ && !clearRequested_) {
        promise.set_value();
        return future;
    }

    // Defer teardown to the graphics thread via pollTaskQueue_(); it runs
    // reset()+shutdown() and then fulfils these promises.
    clearRequested_ = true;
    taskFinished_.store(false, std::memory_order_release);
    clearPromises_.push_back(std::move(promise));
    return future;
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
