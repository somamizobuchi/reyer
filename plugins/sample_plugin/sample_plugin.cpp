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

  protected:
    virtual void onInit() override {};
    virtual void onPause() override {};
    virtual void onResume() override {};
    virtual void onReset() override {};
    virtual void onShutdown() override {}

    virtual void onRender() override {
        DrawFPS(10, 10);
        DrawRectangle(100, 100, 100, 100, getConfiguration().square_color);
    }

    virtual void onData(std::span<const core::Data> data) override {
        auto view = data | std::views::transform(&core::Data::left);
    }

    ~SamplePlugin() = default;
};
}; // namespace reyer::plugin

REYER_PLUGIN_ENTRY(reyer::plugin::SamplePlugin, "Sample plugin", 10)
