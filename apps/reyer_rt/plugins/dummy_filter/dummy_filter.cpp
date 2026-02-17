#include "reyer/core/core.hpp"
#include "reyer/plugin/interfaces.hpp"
#include "reyer/plugin/loader.hpp"

namespace reyer::plugin {

struct DummyFilterConfig {
    float smoothing = 0.5f;
};

class DummyFilter
    : public PluginBase<EyeStageBase, ConfigurableBase<DummyFilterConfig>> {
  public:
    DummyFilter() = default;
    ~DummyFilter() = default;

  protected:
    void onInit() override {
        prev_left_ = {};
        prev_right_ = {};
        has_prev_ = false;
    }
    void onPause() override {}
    void onResume() override {}
    void onReset() override {
        prev_left_ = {};
        prev_right_ = {};
        has_prev_ = false;
    }
    void onShutdown() override {}

    void onProcess(core::EyeData &data) override {
        if (!has_prev_) {
            prev_left_ = data.left.gaze.raw;
            prev_right_ = data.right.gaze.raw;
            has_prev_ = true;
            return;
        }

        float a = getConfig().smoothing;
        float b = 1.0f - a;

        data.left.gaze.filtered.x = a * prev_left_.x + b * data.left.gaze.raw.x;
        data.left.gaze.filtered.y = a * prev_left_.y + b * data.left.gaze.raw.y;
        data.right.gaze.filtered.x = a * prev_right_.x + b * data.right.gaze.raw.x;
        data.right.gaze.filtered.y = a * prev_right_.y + b * data.right.gaze.raw.y;

        prev_left_ = data.left.gaze.raw;
        prev_right_ = data.right.gaze.raw;
    }

  private:
    vec2<float> prev_left_;
    vec2<float> prev_right_;
    bool has_prev_ = false;
};

} // namespace reyer::plugin

REYER_PLUGIN_ENTRY(reyer::plugin::DummyFilter, "Dummy Filter", 1)
