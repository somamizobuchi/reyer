#include "reyer_rt/managers/protocol_manager.hpp"
#include "reyer_rt/managers/graphics_manager.hpp"
#include "reyer_rt/utils/utils.hpp"
#include <format>
#include <spdlog/spdlog.h>
#include <stdexcept>
#include <thread>

namespace reyer_rt::managers {

ProtocolManager::ProtocolManager(
    std::shared_ptr<GraphicsManager> &graphics_manager,
    std::shared_ptr<PluginManager> &plugin_manager,
    std::shared_ptr<BroadcastManager> &broadcast_manager,
    std::shared_ptr<PipelineManager> &pipeline_manager)
    : graphicsManager_(graphics_manager), pluginManager_(plugin_manager),
      broadcastManager_(broadcast_manager),
      pipelineManager_(pipeline_manager) {}

void ProtocolManager::Init() {
    state_.store(State::IDLE, std::memory_order_release);
}

void ProtocolManager::Run() {
    pollCommands_();

    switch (state_.load(std::memory_order_acquire)) {

    case State::IDLE: {
        std::lock_guard<std::mutex> lock(protocolMutex_);
        if (protocolUpdated_) {
            loadProtocol_();
        } else {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
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
        // Check if GraphicsManager reported S key press
        if (auto gfx = graphicsManager_.lock()) {
            if (gfx->ConsumeStartRequest()) {
                EnqueueCommand(net::message::Command::START);
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        break;
    }

    case State::RUNNING: {
        // Task rendering is handled by GraphicsManager.
        // We check if the task finished via GraphicsManager notification.
        auto gfx = graphicsManager_.lock();
        if (gfx && gfx->IsCurrentTaskFinished()) {
            EnqueueCommand(net::message::Command::NEXT);
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(16));
        break;
    }

    case State::SAVING: {
        spdlog::info("Saving data");
        cleanupCurrentTask_();
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

void ProtocolManager::Shutdown() {
    cleanupCurrentTask_();
    currentFile_.reset();
}

void ProtocolManager::cleanupCurrentTask_() {
    if (auto pipeline_mgr = pipelineManager_.lock()) {
        pipeline_mgr->RemoveSink();
    }
    if (eyeDataWriter_) {
        eyeDataWriter_->Stop();
        eyeDataWriter_.reset();
    }
    currentGroup_.reset();

    if (currentTask_) {
        spdlog::info("Shutting down task \"{}\"", currentTask_.getName());
        currentTask_->reset();
        currentTask_->shutdown();

        // Tell GraphicsManager to stop rendering this task
        if (auto gfx = graphicsManager_.lock()) {
            gfx->ClearCurrentTask();
        }
        currentTask_ = reyer::plugin::Plugin();
    }
}

bool ProtocolManager::SetProtocol(
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

std::future<std::error_code>
ProtocolManager::EnqueueCommand(const net::message::Command &command) {
    std::promise<std::error_code> promise{};
    auto future = promise.get_future();
    net::message::CommandPromise cmd{command, std::move(promise)};
    commandQueue_.push(std::move(cmd));
    return future;
}

net::message::RuntimeState ProtocolManager::GetRuntimeState() const {
    auto current_state = state_.load(std::memory_order_acquire);
    switch (current_state) {
    case State::IDLE: {
        // If graphics are initialized, report STANDBY so client knows it's ready
        if (auto gfx = graphicsManager_.lock()) {
            if (gfx->IsGraphicsInitialized()) {
                return net::message::RuntimeState::STANDBY;
            }
        }
        return net::message::RuntimeState::DEFAULT;
    }
    case State::STANDBY:
        return net::message::RuntimeState::STANDBY;
    case State::RUNNING:
    case State::SAVING:
        return net::message::RuntimeState::RUNNING;
    default:
        return net::message::RuntimeState::DEFAULT;
    }
}

void ProtocolManager::loadProtocol_() {
    protocolUpdated_ = false;

    if (!currentProtocol_) {
        state_.store(State::IDLE, std::memory_order_release);
        currentTaskIndex_ = 0;
        if (auto gfx = graphicsManager_.lock()) {
            gfx->ClearStandbyInfo();
        }
        return;
    }

    state_.store(State::STANDBY, std::memory_order_release);

    // Tell GraphicsManager to show the protocol name on standby screen
    if (auto gfx = graphicsManager_.lock()) {
        gfx->SetStandbyInfo(currentProtocol_->name);
    }

    // Broadcast PROTOCOL_LOADED
    if (auto bcast = broadcastManager_.lock()) {
        net::message::ProtocolEventMessage event{
            .protocol_uuid = {},
            .event = net::message::ProtocolEvent::PROTOCOL_LOADED,
            .data = 0,
            .protocol_name = currentProtocol_->name,
            .participant_id = currentProtocol_->participant_id,
            .notes = currentProtocol_->notes,
            .tasks = currentProtocol_->tasks,
        };
        bcast->Broadcast(net::message::BroadcastTopic::PROTOCOL, event);
    }
}

void ProtocolManager::pollCommands_() {
    if (auto cmd = commandQueue_.try_pop()) {
        auto state = state_.load(std::memory_order_acquire);
        switch (cmd.value().command) {
        case net::message::Command::START:
            if (state == State::STANDBY) {
                {
                    std::lock_guard<std::mutex> lock(protocolMutex_);
                    if (currentProtocol_) {
                        currentProtocol_->protocol_uuid = utils::uuid_v4();
                        spdlog::info("Generated run UUID: {}",
                                     currentProtocol_->protocol_uuid);
                    }
                }

                // Create HDF5 file
                if (currentProtocol_) {
                    auto filename = std::format(
                        "/tmp/{}.h5", currentProtocol_->protocol_uuid);
                    currentFile_ = std::make_shared<reyer::h5::File>(
                        filename, H5F_ACC_TRUNC);

                    if (auto bcast = broadcastManager_.lock()) {
                        net::message::ProtocolEventMessage event{
                            .protocol_uuid = currentProtocol_->protocol_uuid,
                            .event = net::message::ProtocolEvent::PROTOCOL_NEW,
                            .data = 0,
                            .protocol_name = currentProtocol_->name,
                            .participant_id = currentProtocol_->participant_id,
                            .notes = currentProtocol_->notes,
                            .tasks = currentProtocol_->tasks,
                            .file_path = filename,
                        };
                        bcast->Broadcast(
                            net::message::BroadcastTopic::PROTOCOL, event);
                    }
                }

                loadTask_(LoadCommand::FIRST);
            }
            break;
        case net::message::Command::STOP:
            if (state == State::RUNNING)
                loadTask_(LoadCommand::FINISH);
            break;
        case net::message::Command::NEXT:
            if (state == State::RUNNING)
                loadTask_(LoadCommand::NEXT);
            break;
        case net::message::Command::EXIT:
            if (state == State::RUNNING) {
                state_.store(State::SAVING, std::memory_order_release);
            }
            break;
        }
        cmd.value().promise.set_value(std::error_code{});
    }
}

void ProtocolManager::loadTask_(const LoadCommand &command) {
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

    // Clean up current task
    if (currentTask_) {
        if (auto pipeline_mgr = pipelineManager_.lock()) {
            pipeline_mgr->RemoveSink();
        }
        if (eyeDataWriter_) {
            eyeDataWriter_->Stop();
            eyeDataWriter_.reset();
        }
        currentGroup_.reset();

        spdlog::info("Shutting down task \"{}\"", currentTask_.getName());
        currentTask_->reset();
        currentTask_->shutdown();

        // Remove from graphics
        if (auto gfx = graphicsManager_.lock()) {
            gfx->ClearCurrentTask();
        }

        net::message::ProtocolEventMessage event{
            currentProtocol_->protocol_uuid,
            net::message::ProtocolEvent::TASK_END, currentTaskIndex_};
        if (auto ec = broadcast_manager->Broadcast(
                net::message::BroadcastTopic::PROTOCOL, event)) {
            spdlog::warn("Failed to send broadcast message: {}", ec.message());
        }
    }

    if (nextIndex >= static_cast<int>(currentProtocol_->tasks.size())) {
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

    // Set render context and init — must happen before handing to graphics
    auto gfx = graphicsManager_.lock();
    if (gfx) {
        auto ctx = gfx->GetRenderContext();
        if (auto *render = currentTask_.as<reyer::plugin::IRender>()) {
            render->setRenderContext(ctx);
        }
    }

    spdlog::info("Initializing task \"{}\"", currentTask_.getName());
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

    // Hand the task to GraphicsManager for rendering
    if (gfx) {
        gfx->SetCurrentTask(currentTask_);
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

} // namespace reyer_rt::managers
