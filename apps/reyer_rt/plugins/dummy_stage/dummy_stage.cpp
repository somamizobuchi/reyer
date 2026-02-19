#include "reyer/core/core.hpp"
#include "reyer/plugin/interfaces.hpp"
#include "reyer/plugin/loader.hpp"

namespace reyer::plugin {

struct DummyStageConfig {
    float scale_factor = 1.0f;
};

class DummyStage
    : public PluginBase<EyeStageBase, ConfigurableBase<DummyStageConfig>> {
  public:
    DummyStage() = default;
    ~DummyStage() = default;

  protected:
    void onInit() override {}
    void onPause() override {}
    void onResume() override {}
    void onReset() override {}
    void onShutdown() override {}

    void onProcess(core::EyeData &data) override {
        float scale = getConfig().scale_factor;
        data.left.gaze.raw.x *= scale;
        data.left.gaze.raw.y *= scale;
        data.right.gaze.raw.x *= scale;
        data.right.gaze.raw.y *= scale;
    }
};

} // namespace reyer::plugin

REYER_PLUGIN_ENTRY(reyer::plugin::DummyStage, "Dummy Stage", "Soma Mizobuchi",
                   "A dummy stage plugin that scales gaze data.", 1)
