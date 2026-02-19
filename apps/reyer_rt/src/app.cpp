#include "reyer_rt/app.hpp"
#include "reyer_rt/experiment/protocol.hpp"
#include "reyer_rt/managers/graphics_manager.hpp"
#include "reyer_rt/managers/plugin_manager.hpp"
#include <filesystem>
#include <format>
#include <memory>
#include <print>
#include <spdlog/spdlog.h>
#include <vector>

namespace reyer_rt {
App::App(int argc, char **argv) {
    if (argc > 0) {
        executableDir_ =
            std::filesystem::canonical(argv[0]).parent_path();
    }
}

void App::Launch() {
    std::vector<std::filesystem::path> plugin_dirs = {
        executableDir_ / "plugins",
    };
    if (const char *home = std::getenv("HOME"); home) {
        plugin_dirs.emplace_back(
            std::filesystem::path(home) / ".local/share/reyer/plugins");
    }
    pluginManager_ = std::make_shared<managers::PluginManager>(
        std::move(plugin_dirs));
    broadcastManager_ = std::make_shared<managers::BroadcastManager>();
    pipelineManager_ = std::make_shared<managers::PipelineManager>();
    graphicsManager_ = std::make_shared<managers::GraphicsManager>(
        broadcastManager_, pipelineManager_);
    protocolManager_ = std::make_shared<managers::ProtocolManager>(
        graphicsManager_, pluginManager_, broadcastManager_, pipelineManager_);
    messageManager_ = std::make_shared<managers::MessageManager>(
        graphicsManager_, pluginManager_, pipelineManager_, protocolManager_);

    messageManager_->Spawn();
    broadcastManager_->Spawn();
    pipelineManager_->Spawn();
    protocolManager_->Spawn();
    graphicsManager_->Init();

    graphicsManager_->Run();

    protocolManager_->Stop();
    pipelineManager_->Stop();
    broadcastManager_->Stop();
    messageManager_->Stop();
}

App::~App() {}

} // namespace reyer_rt
