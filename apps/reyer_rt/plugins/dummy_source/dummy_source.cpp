#include "reyer/core/core.hpp"
#include "reyer/plugin/interfaces.hpp"
#include "reyer/plugin/loader.hpp"
#include <thread>

namespace reyer::plugin {

struct DummySourceConfig {
    float sample_rate = 60.0f;
};

class DummySource : public SourcePluginBase<DummySourceConfig> {
  public:
    DummySource() = default;
    ~DummySource() = default;

  protected:
    void onInit() override { frame_count_ = 0; }
    void onPause() override {}
    void onResume() override {}
    void onReset() override { frame_count_ = 0; }
    void onShutdown() override {}

    bool onProduce(core::EyeData &out) override {
        auto interval = std::chrono::milliseconds(
            static_cast<int>(1000.0f / getConfig().sample_rate));
        std::this_thread::sleep_for(interval);

        float val = static_cast<float>(frame_count_ % 100);
        reyer::vec2<float> point{val, val};
        out = core::EyeData{
            .left = {.dpi{.p1 = point, .p4 = point}, .gaze{point}},
            .right = {.dpi{.p1 = point, .p4 = point}, .gaze{point}},
            .timestamp = frame_count_,
        };
        ++frame_count_;
        return true;
    }

  private:
    uint64_t frame_count_ = 0;
};

} // namespace reyer::plugin

REYER_PLUGIN_ENTRY(reyer::plugin::DummySource, "Dummy Source", 1)
