#include "reyer/core/core.hpp"
#include "reyer/plugin/interfaces.hpp"
#include "reyer/plugin/loader.hpp"

namespace reyer::plugin {

struct SimpleFilterConfig {
    float smoothing = 0.5f;
};

class SimpleFilter
    : public PluginBase<EyeStageBase, ConfigurableBase<SimpleFilterConfig>> {
  public:
    SimpleFilter() = default;
    ~SimpleFilter() = default;

  protected:
    void onInit() override { resetState(); }
    void onPause() override {}
    void onResume() override {}
    void onReset() override { resetState(); }
    void onShutdown() override {}

    void onProcess(core::EyeData &data) override {
        if (!has_prev_) {
            data.left.gaze.filtered = data.left.gaze.raw;
            data.right.gaze.filtered = data.right.gaze.raw;
            data.left.gaze.velocity = {};
            data.right.gaze.velocity = {};
            left_ = {data.left.gaze.raw, {}, data.left.gaze.raw};
            right_ = {data.right.gaze.raw, {}, data.right.gaze.raw};
            prev_timestamp_ = data.timestamp;
            has_prev_ = true;
            return;
        }

        float a = getConfig().smoothing;
        float dt = static_cast<float>(data.timestamp - prev_timestamp_);

        filterEye(a, 0.001, data.left.gaze, left_);
        filterEye(a, 0.001, data.right.gaze, right_);

        prev_timestamp_ = data.timestamp;
    }

  private:
    struct EyeState {
        vec2<float> filtered_pos;
        vec2<float> filtered_vel;
        vec2<float> raw_pos;
    };

    static vec2<float> ema(float a, vec2<float> x, vec2<float> y_prev) {
        float b = 1.0f - a;
        return {a * x.x + b * y_prev.x, a * x.y + b * y_prev.y};
    }

    static void filterEye(float a, float dt, core::GazeData &gaze,
                          EyeState &state) {
        gaze.filtered = ema(a, gaze.raw, state.filtered_pos);

        if (dt > 0.0f) {
            vec2<float> raw_vel = {(gaze.raw.x - state.raw_pos.x) / dt,
                                   (gaze.raw.y - state.raw_pos.y) / dt};
            gaze.velocity = ema(a, raw_vel, state.filtered_vel);
        }

        state.filtered_pos = gaze.filtered;
        state.filtered_vel = gaze.velocity;
        state.raw_pos = gaze.raw;
    }

    void resetState() {
        left_ = {};
        right_ = {};
        prev_timestamp_ = 0;
        has_prev_ = false;
    }

    EyeState left_;
    EyeState right_;
    uint64_t prev_timestamp_ = 0;
    bool has_prev_ = false;
};

} // namespace reyer::plugin

REYER_PLUGIN_ENTRY(reyer::plugin::SimpleFilter, "Simple Filter",
                   "Soma Mizobuchi", "Simple Filter", 1);
