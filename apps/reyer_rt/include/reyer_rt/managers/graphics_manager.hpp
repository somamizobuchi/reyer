#pragma once

#include <atomic>
#include <cstdarg>
#include <cstdint>
#include <future>
#include <memory>
#include <mutex>
#include <optional>
#include <system_error>

#include "reyer_rt/managers/broadcast_manager.hpp"
#include "reyer_rt/managers/pipeline_manager.hpp"
#include "reyer_rt/net/message_types.hpp"
#include <reyer/core/queue.hpp>
#include <reyer/graphics/graphics.hpp>
#include <reyer/plugin/loader.hpp>
#include <vector>

namespace reyer_rt::managers {

class GraphicsManager {
  public:
    explicit GraphicsManager(
        std::shared_ptr<BroadcastManager> &broadcast_manager,
        std::shared_ptr<PipelineManager> &pipeline_manager);

    void Init();
    void Run();
    void Shutdown();

    std::future<std::error_code>
    SetGraphicsSettings(const net::message::GraphicsSettingsRequest &settings);

    std::vector<net::message::MonitorInfo> GetMonitorInfo();

    std::optional<net::message::GraphicsSettings>
    GetCurrentGraphicsSettings() const;

    reyer::core::RenderContext GetRenderContext() const;

    bool IsGraphicsInitialized() const;

    // Task management — called by ProtocolManager from its thread.
    // These are thread-safe via queue.
    void SetCurrentTask(reyer::plugin::Plugin task);
    void ClearCurrentTask();
    bool IsCurrentTaskFinished() const;

    // Protocol info for standby screen — called by ProtocolManager
    void SetStandbyInfo(const std::string &protocol_name);
    void ClearStandbyInfo();

    bool IsStopRequested() const;
    void RequestStop();

    // Returns true once if S key was pressed on standby screen (consumed on read)
    bool ConsumeStartRequest();

    std::vector<net::message::MonitorInfo> monitors_;

    ~GraphicsManager() = default;

  private:
    enum class State : uint8_t {
        DEFAULT,  // Before graphics window created
        READY,    // Window open, rendering standby or active task
    };

    void pollMonitors_();
    void showStandbyScreen_();
    void
    applyGraphicsSettings_(net::message::GraphicsSettingsPromise &gfx_promise);
    static void errorCallback_(int code, const char *message, va_list args);
    void pollTaskQueue_();

    std::atomic<State> state_{};
    std::atomic<bool> stop_requested_{false};

    std::weak_ptr<BroadcastManager> broadcastManager_;
    std::weak_ptr<PipelineManager> pipelineManager_;

    std::optional<net::message::GraphicsSettingsRequest> graphicsSettings_;
    std::atomic<bool> graphicsInitialized_{false};

    reyer::core::RenderContext renderContext_;

    reyer::core::Queue<net::message::GraphicsSettingsPromise>
        graphicsSettingsQueue_;

    // Current task being rendered (set by ProtocolManager via queue)
    std::mutex taskMutex_;
    reyer::plugin::Plugin currentTask_;
    reyer::plugin::Plugin pendingTask_; // waiting to be init'd on gfx thread
    std::atomic<bool> taskFinished_{false};

    // Standby screen info
    std::mutex standbyMutex_;
    std::optional<std::string> standbyProtocolName_;

    std::atomic<bool> startRequested_{false};
};

} // namespace reyer_rt::managers
