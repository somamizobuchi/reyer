#include "reyer_rt/managers/protocol_manager.hpp"
#include "reyer_rt/managers/graphics_manager.hpp"
#include "reyer_rt/net/message_types.hpp"
#include "reyer_rt/utils/utils.hpp"
#include <chrono>
#include <format>
#include <glaze/glaze.hpp>
#include <optional>
#include <spdlog/spdlog.h>
#include <stdexcept>
#include <thread>

namespace reyer_rt::managers {

namespace {

// Mirrors experiment::Task, but embeds the plugin's configuration as raw JSON
// so it isn't re-encoded as an escaped string.
struct TaskConfig {
    std::string name{};
    glz::raw_json configuration{};
};

// The complete configuration of a run, serialized to the file's config_json
// attribute. Optionals are absent when a protocol starts before graphics have
// been initialized.
struct RunConfig {
    std::string protocol_uuid{};
    std::string protocol_name{};
    std::string participant_id{};
    std::string notes{};
    uint64_t start_time_ns{};
    std::string start_time_iso{};
    std::optional<net::message::GraphicsSettings> graphics{};
    std::optional<uint32_t> view_distance_mm{};
    std::optional<net::message::MonitorInfo> monitor{};
    std::optional<reyer::core::RenderContext> render_context{};
    std::vector<TaskConfig> tasks{};
};

} // namespace

ProtocolManager::ProtocolManager(
    std::shared_ptr<GraphicsManager> &graphics_manager,
    std::shared_ptr<PluginManager> &plugin_manager,
    std::shared_ptr<BroadcastManager> &broadcast_manager,
    std::shared_ptr<PipelineManager> &pipeline_manager)
    : graphicsManager_(graphics_manager), pluginManager_(plugin_manager),
      broadcastManager_(broadcast_manager),
      pipelineManager_(pipeline_manager) {}

void ProtocolManager::Init() {
    state_.store(State::STANDBY, std::memory_order_release);
}

void ProtocolManager::Run() {
    pollCommands_();

    switch (state_.load(std::memory_order_acquire)) {

    case State::STANDBY: {
        {
            std::lock_guard<std::mutex> lock(protocolMutex_);
            if (protocolUpdated_)
                loadProtocol_();
        }
        if (currentProtocol_) {
            if (auto gfx = graphicsManager_.lock()) {
                if (gfx->ConsumeStartRequest())
                    startProtocol_();
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
            // Teardown is handled by loadTask_(NEXT) below (on the gfx thread).
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
        spdlog::info("Saving complete");

        if (auto bcast = broadcastManager_.lock()) {
            net::message::ProtocolEventMessage event{
                .protocol_uuid = currentProtocol_ ? currentProtocol_->protocol_uuid : "",
                .event = net::message::ProtocolEvent::PROTOCOL_COMPLETE,
                .data = 0,
            };
            bcast->Broadcast(net::message::BroadcastTopic::PROTOCOL, event);
        }

        if (exitRequested_) {
            exitRequested_ = false;
            if (auto gfx = graphicsManager_.lock()) {
                gfx->RequestStop();
            }
        } else {
            state_.store(State::STANDBY, std::memory_order_release);
        }
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

    if (currentTask_) {
        // Teardown (reset+shutdown) runs on the graphics thread. Wait for it
        // to complete before releasing our handle so ordering is preserved.
        // This precedes tearing the writer down so a task can still record
        // from its reset()/shutdown().
        if (auto gfx = graphicsManager_.lock()) {
            gfx->ClearCurrentTask().wait();
        }
        currentTask_ = reyer::plugin::Plugin();
    }

    if (taskDataWriter_) {
        taskDataWriter_->Stop();
        taskDataWriter_.reset();
    }
    currentGroup_.reset();
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
    if (auto gfx = graphicsManager_.lock()) {
        if (!gfx->IsGraphicsInitialized())
            return net::message::RuntimeState::DEFAULT;
    }
    switch (state_.load(std::memory_order_acquire)) {
    case State::STANDBY:
        return net::message::RuntimeState::STANDBY;
    case State::RUNNING:
    case State::SAVING:
        return net::message::RuntimeState::RUNNING;
    }
    return net::message::RuntimeState::DEFAULT;
}

void ProtocolManager::loadProtocol_() {
    protocolUpdated_ = false;

    if (!currentProtocol_) {
        currentTaskIndex_ = 0;
        if (auto gfx = graphicsManager_.lock()) {
            gfx->ClearStandbyInfo();
        }
        return;
    }

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

void ProtocolManager::startProtocol_() {
    {
        std::lock_guard<std::mutex> lock(protocolMutex_);
        if (currentProtocol_) {
            currentProtocol_->protocol_uuid = utils::uuid_v4();
            spdlog::info("Generated run UUID: {}",
                         currentProtocol_->protocol_uuid);
        }
    }

    if (!currentProtocol_)
        return;

    auto filename =
        std::format("/tmp/{}.h5", currentProtocol_->protocol_uuid);
    currentFile_ =
        std::make_shared<reyer::h5::File>(filename, H5F_ACC_TRUNC);

    protocolEpochUs_ = reyer::core::now_us();

    writeRunConfig_();

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
        bcast->Broadcast(net::message::BroadcastTopic::PROTOCOL, event);
    }

    loadTask_(LoadCommand::FIRST);
}

void ProtocolManager::writeRunConfig_() {
    if (!currentFile_ || !currentProtocol_)
        return;

    const auto &protocol = *currentProtocol_;
    const hid_t root = currentFile_->get();

    auto now = std::chrono::system_clock::now();
    RunConfig config{
        .protocol_uuid = protocol.protocol_uuid,
        .protocol_name = protocol.name,
        .participant_id = protocol.participant_id,
        .notes = protocol.notes,
        .start_time_ns =
            static_cast<uint64_t>(std::chrono::duration_cast<
                                      std::chrono::nanoseconds>(
                                      now.time_since_epoch())
                                      .count()),
        .start_time_iso = std::format(
            "{:%FT%TZ}", std::chrono::floor<std::chrono::seconds>(now)),
    };

    if (auto gfx = graphicsManager_.lock()) {
        if (auto settings = gfx->GetCurrentGraphicsSettingsRequest()) {
            config.graphics = settings->graphics_settings;
            config.view_distance_mm = settings->view_distance_mm;
            config.render_context = gfx->GetRenderContext();

            for (const auto &monitor : gfx->GetMonitorInfo()) {
                if (monitor.index == settings->graphics_settings.monitor_index) {
                    config.monitor = monitor;
                    break;
                }
            }
        } else {
            spdlog::warn("Graphics not initialized; run config will omit "
                         "graphics settings");
        }
    }

    config.tasks.reserve(protocol.tasks.size());
    for (const auto &task : protocol.tasks) {
        config.tasks.emplace_back(
            task.name, glz::raw_json{task.configuration.empty()
                                         ? "null"
                                         : task.configuration});
    }

    try {
        // Typed attributes for the fields runs get filtered and grouped on;
        // config_json below carries the whole thing without loss.
        reyer::h5::set_attr(root, "protocol_uuid", config.protocol_uuid);
        reyer::h5::set_attr(root, "protocol_name", config.protocol_name);
        reyer::h5::set_attr(root, "participant_id", config.participant_id);
        reyer::h5::set_attr(root, "notes", config.notes);
        reyer::h5::set_attr(root, "start_time_ns", config.start_time_ns);
        reyer::h5::set_attr(root, "start_time_iso", config.start_time_iso);
        reyer::h5::set_attr(root, "task_count",
                            static_cast<int>(config.tasks.size()));

        if (config.graphics) {
            const auto &gs = *config.graphics;
            reyer::h5::set_attr(root, "monitor_index", gs.monitor_index);
            reyer::h5::set_attr(root, "vsync", gs.vsync);
            reyer::h5::set_attr(root, "full_screen", gs.full_screen);
            reyer::h5::set_attr(root, "anti_aliasing", gs.anti_aliasing);
            reyer::h5::set_attr(root, "target_fps", gs.target_fps);
            reyer::h5::set_attr(root, "width", gs.width);
            reyer::h5::set_attr(root, "height", gs.height);
        }
        if (config.view_distance_mm) {
            reyer::h5::set_attr(root, "view_distance_mm",
                                *config.view_distance_mm);
        }
        if (config.monitor) {
            const auto &m = *config.monitor;
            reyer::h5::set_attr(root, "monitor_name", m.name);
            reyer::h5::set_attr(root, "monitor_width_px", m.width_px);
            reyer::h5::set_attr(root, "monitor_height_px", m.height_px);
            reyer::h5::set_attr(root, "monitor_width_mm", m.width_mm);
            reyer::h5::set_attr(root, "monitor_height_mm", m.height_mm);
            reyer::h5::set_attr(root, "monitor_refresh_rate", m.refresh_rate);
        }
        if (config.render_context) {
            reyer::h5::set_attr(root, "ppd_x", config.render_context->ppd_x);
            reyer::h5::set_attr(root, "ppd_y", config.render_context->ppd_y);
        }

        std::string config_json;
        if (auto ec = glz::write_json(config, config_json)) {
            spdlog::warn("Failed to serialize run config: {}",
                         glz::format_error(ec));
        } else {
            reyer::h5::set_attr(root, "config_json", config_json);
        }
    } catch (const std::exception &e) {
        // A run without its config attributes is still worth recording.
        spdlog::error("Failed to write run config: {}", e.what());
    }
}

void ProtocolManager::pollCommands_() {
    if (auto cmd = commandQueue_.try_pop()) {
        auto state = state_.load(std::memory_order_acquire);
        switch (cmd.value().command) {
        case net::message::Command::START:
            if (state == State::STANDBY)
                startProtocol_();
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
            exitRequested_ = true;
            if (state == State::RUNNING) {
                state_.store(State::SAVING, std::memory_order_release);
            } else {
                if (auto gfx = graphicsManager_.lock()) {
                    gfx->RequestStop();
                }
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

        // Teardown (reset+shutdown) runs on the graphics thread. Wait for it
        // to complete before loading the next task so shutdown() strictly
        // precedes the next task's init(). The writer is retired afterwards so
        // a task can still record from its reset()/shutdown().
        if (auto gfx = graphicsManager_.lock()) {
            gfx->ClearCurrentTask().wait();
        }

        if (taskDataWriter_) {
            taskDataWriter_->Stop();
            taskDataWriter_.reset();
        }
        currentGroup_.reset();

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
    auto plugin = plg_mngr->CreateInstance(task.name); // fresh instance per run

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

    // Create the group and writer before staging the task: the graphics thread
    // injects the recorder and calls init() as soon as it sees the task, so the
    // writer must already exist or a task recording from onInit() would find no
    // recorder.
    if (currentFile_) {
        auto group_name = std::format("task_{:03d}", currentTaskIndex_);
        currentGroup_ =
            std::make_unique<reyer::h5::Group>(currentFile_->get(), group_name);
        currentGroup_->set_attr("task_name", task.name);
        currentGroup_->set_attr("task_index",
                                static_cast<int>(currentTaskIndex_));
        if (!task.configuration.empty()) {
            currentGroup_->set_attr("configuration", task.configuration);
        }

        // t=0 for everything in this group. Recorded relative to the protocol
        // start so timestamps can be lined up across tasks.
        auto task_epoch_us = reyer::core::now_us();
        currentGroup_->set_attr("t0_offset_us",
                                task_epoch_us > protocolEpochUs_
                                    ? task_epoch_us - protocolEpochUs_
                                    : uint64_t{0});

        taskDataWriter_ = std::make_shared<stages::TaskDataWriter>(
            currentGroup_->get(), task_epoch_us);
    }

    // Hand the task to GraphicsManager — it will ChangeDirectory, setRenderContext,
    // setRecorder, and call init() on the graphics thread before rendering starts.
    if (auto gfx = graphicsManager_.lock()) {
        gfx->SetCurrentTask(currentTask_, taskDataWriter_.get());
    }

    // Set current task as pipeline sink, then attach the writer alongside it.
    if (auto pipeline_mgr = pipelineManager_.lock()) {
        pipeline_mgr->ReplaceSink(currentTask_);

        if (taskDataWriter_) {
            // Hand the pipeline a shared_ptr<ISink> that shares ownership with
            // taskDataWriter_ (aliasing ctor) so it keeps the writer alive for
            // as long as it is registered as a sink. ISink is a virtual base,
            // so this relies on the implicit upcast, not static_pointer_cast.
            pipeline_mgr->AddSink(
                std::shared_ptr<reyer::plugin::ISink<reyer::core::EyeData>>(
                    taskDataWriter_, taskDataWriter_.get()));
            taskDataWriter_->Spawn();
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

} // namespace reyer_rt::managers
