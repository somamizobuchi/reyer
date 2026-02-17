#pragma once

#include <atomic>
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
#include "reyer_rt/threading/thread.hpp"
#include <reyer/core/h5.hpp>
#include <reyer/core/queue.hpp>
#include <reyer/plugin/loader.hpp>

namespace reyer_rt::managers {

class GraphicsManager;

class ProtocolManager : public threading::Thread<ProtocolManager> {
  public:
    explicit ProtocolManager(
        std::shared_ptr<GraphicsManager> &graphics_manager,
        std::shared_ptr<PluginManager> &plugin_manager,
        std::shared_ptr<BroadcastManager> &broadcast_manager,
        std::shared_ptr<PipelineManager> &pipeline_manager);

    void Init();
    void Run();
    void Shutdown();

    bool SetProtocol(const net::message::ProtocolRequest &protocol);

    std::future<std::error_code>
    EnqueueCommand(const net::message::Command &command);

    net::message::RuntimeState GetRuntimeState() const;

    ~ProtocolManager() = default;

  private:
    enum class State : uint8_t {
        IDLE,
        STANDBY,
        RUNNING,
        SAVING,
    };

    enum class LoadCommand : uint8_t { FIRST, NEXT, PREV, LAST, FINISH };

    void pollCommands_();
    void loadProtocol_();
    void loadTask_(const LoadCommand &command);
    void cleanupCurrentTask_();

    std::atomic<State> state_{State::IDLE};

    std::weak_ptr<GraphicsManager> graphicsManager_;
    std::weak_ptr<PluginManager> pluginManager_;
    std::weak_ptr<BroadcastManager> broadcastManager_;
    std::weak_ptr<PipelineManager> pipelineManager_;

    std::mutex protocolMutex_;
    std::optional<net::message::ProtocolRequest> currentProtocol_;
    bool protocolUpdated_{false};

    reyer::plugin::Plugin currentTask_;
    size_t currentTaskIndex_{0};

    reyer::core::Queue<net::message::CommandPromise> commandQueue_;

    std::shared_ptr<reyer::h5::File> currentFile_;
    std::unique_ptr<reyer::h5::Group> currentGroup_;
    std::unique_ptr<stages::EyeDataWriter> eyeDataWriter_;
};

} // namespace reyer_rt::managers
