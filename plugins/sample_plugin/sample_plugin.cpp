#include "configuration.hpp"
#include "reyer/core/core.hpp"
#include "reyer/plugin/defs.hpp"
#include "reyer/plugin/iplugin.hpp"
#include <ranges>
#include <raylib.h>

namespace reyer::plugin {

class SamplePlugin : public RenderPluginBase<SampleConfiguration> {
  public:
    SamplePlugin() {};
    ~SamplePlugin() = default;

  protected:
    virtual void onInit() override {};
    virtual void onPause() override {};
    virtual void onResume() override {};
    virtual void onReset() override {};
    virtual void onShutdown() override {}

    virtual void onRender() override {
        DrawFPS(10, 10);
        DrawRectanglePro({GetScreenWidth() / 2.0f, GetScreenHeight() / 2.0f, 100, 100}, {50, 50}, 0,getConfiguration().square_color);
    }

    virtual void onData(std::span<const core::Data> data) override {
        auto view = data | std::views::transform(&core::Data::left);
    }
};
}; // namespace reyer::plugin

REYER_PLUGIN_ENTRY(reyer::plugin::SamplePlugin, "Sample plugin", 10)
