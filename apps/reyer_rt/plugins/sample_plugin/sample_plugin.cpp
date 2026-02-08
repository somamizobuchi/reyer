#include "configuration.hpp"
#include "reyer/core/core.hpp"
#include "reyer/plugin/interfaces.hpp"
#include "reyer/plugin/loader.hpp"
#include <ranges>
#include <raylib.h>

namespace reyer::plugin {

class SamplePlugin : public RenderPluginBase<SampleConfiguration> {
  public:
    SamplePlugin() {};
    ~SamplePlugin() = default;

  protected:
    virtual void onInit() override {
        rectangle_ = {GetScreenWidth() / 2.0f, GetScreenHeight() / 2.0f, 100,
                      100};
    };
    virtual void onPause() override {};
    virtual void onResume() override {};
    virtual void onReset() override {};
    virtual void onShutdown() override {}

    virtual void onRender() override {
        DrawFPS(10, 10);
        DrawRectanglePro(rectangle_, {50, 50}, 0, getConfig().square_color);
    }

    virtual void onConsume(const core::EyeData &data) override {
        rectangle_.x = static_cast<float>(data.left.dpi.p1.x - data.left.dpi.p4.x);
        rectangle_.y = static_cast<float>(data.left.dpi.p4.y - data.left.dpi.p1.y);
    }

  private:
    Rectangle rectangle_{};
};
}; // namespace reyer::plugin

REYER_PLUGIN_ENTRY(reyer::plugin::SamplePlugin, "Sample plugin", 10)
