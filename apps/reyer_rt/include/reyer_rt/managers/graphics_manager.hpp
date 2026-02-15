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
#include "reyer_rt/managers/plugin_manager.hpp"
#include "reyer_rt/net/message_types.hpp"
#include "reyer_rt/stages/eye_data_writer.hpp"
#include <reyer/core/h5.hpp>
#include <reyer/core/queue.hpp>
#include <reyer/graphics/graphics.hpp>
#include <reyer/plugin/loader.hpp>
#include <vector>

namespace reyer_rt::managers {

class GraphicsManager {
  public:
    explicit GraphicsManager(
        std::shared_ptr<PluginManager> &plugin_manager,
        std::shared_ptr<BroadcastManager> &broadcast_manager,
        std::shared_ptr<PipelineManager> &pipeline_manager);

    void Init();

    void Run();

    void Shutdown();

    std::future<std::error_code>
    SetGraphicsSettings(const net::message::GraphicsSettingsRequest &settings);

    bool SetProtocol(const net::message::ProtocolRequest &protocol);

    std::future<std::error_code>
    EnqueueCommand(const net::message::Command &command);

    std::vector<net::message::MonitorInfo> GetMonitorInfo();

    net::message::RuntimeState GetRuntimeState() const;

    std::optional<net::message::GraphicsSettings>
    GetCurrentGraphicsSettings() const;

    std::vector<net::message::MonitorInfo> monitors_;

    ~GraphicsManager() = default;

  private:
    enum class State : uint8_t {
        DEFAULT,
        STANDBY,
        RUNNING,
        SAVING,
    };

    enum class LoadCommand : uint8_t { FIRST, NEXT, PREV, LAST, FINISH };

    std::atomic<State> state_{};

    void pollConfigRequest_();

    void pollMonitors_();

    void pollCommands_();

    void loadProtocol_();

    void loadTask_(const LoadCommand &command);

    void showStandbyScreen_();

    void
    applyGraphicsSettings_(net::message::GraphicsSettingsPromise &gfx_promise);

    static void errorCallback_(int code, const char *message, va_list args);

    std::atomic<bool> stop_requested_{false};

    std::weak_ptr<managers::PluginManager> pluginManager_;
    std::weak_ptr<managers::BroadcastManager> broadcastManager_;
    std::weak_ptr<managers::PipelineManager> pipelineManager_;

    std::mutex protocolMutex_;
    std::optional<net::message::ProtocolRequest> currentProtocol_;
    bool protocolUpdated_{false};
    bool requiresWindowReload{false};

    std::optional<net::message::GraphicsSettingsRequest> graphicsSettings_;
    bool graphicsInitialized_{false};

    reyer::plugin::Plugin currentTask_;
    size_t currentTaskIndex_{0};

    reyer::core::RenderContext renderContext_;

    reyer::core::Queue<net::message::CommandPromise> commandQueue_;
    reyer::core::Queue<net::message::GraphicsSettingsPromise>
        graphicsSettingsQueue_;

    std::shared_ptr<reyer::h5::File> currentFile_;
    std::unique_ptr<reyer::h5::Group> currentGroup_;
    std::unique_ptr<stages::EyeDataWriter> eyeDataWriter_;
};

} // namespace reyer_rt::managers
