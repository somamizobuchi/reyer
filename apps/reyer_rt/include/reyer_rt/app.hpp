#pragma once

#include "managers/graphics_manager.hpp"
#include "managers/plugin_manager.hpp"
#include "managers/message_manager.hpp"
#include "managers/broadcast_manager.hpp"
#include <memory>

namespace reyer_rt {

class App {
  public:
    explicit App(int argc, char **argv);

    void Launch();

    ~App();

  private:
    std::shared_ptr<managers::GraphicsManager> graphicsManager_;
    std::shared_ptr<managers::PluginManager> pluginManager_;
    std::shared_ptr<managers::MessageManager> messageManager_;
    std::shared_ptr<managers::BroadcastManager> broadcastManager_;
};

} // namespace reyer_rt
