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
    pluginManager_ = std::make_shared<managers::PluginManager>(
        executableDir_ / "plugins");
    broadcastManager_ = std::make_shared<managers::BroadcastManager>();
    pipelineManager_ = std::make_shared<managers::PipelineManager>();
    graphicsManager_ = std::make_shared<managers::GraphicsManager>(
        pluginManager_, broadcastManager_, pipelineManager_);
    messageManager_ = std::make_shared<managers::MessageManager>(
        graphicsManager_, pluginManager_, pipelineManager_);

    auto load_errors = pluginManager_->GetLoadErrors();
    if (!load_errors.empty()) {
        spdlog::warn("Warning: {} plugin(s) failed to load:",
                     load_errors.size());
        for (const auto &[path, ec] : load_errors) {
            spdlog::warn("  - {}: {}", path, ec.message());
        }
    }

    messageManager_->Spawn();
    broadcastManager_->Spawn();
    pipelineManager_->Spawn();
    graphicsManager_->Init();

    graphicsManager_->Run();

    pipelineManager_->Stop();
    broadcastManager_->Stop();
    messageManager_->Stop();
}

App::~App() {}

} // namespace reyer_rt
