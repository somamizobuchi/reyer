#include "reyer/core/core.hpp"
#include "reyer/plugin/interfaces.hpp"
#include "reyer/plugin/loader.hpp"

namespace reyer::plugin {

struct DummyCalibrationConfig {
    float offset_x = 0.0f;
    float offset_y = 0.0f;
};

class DummyCalibration
    : public PluginBase<CalibrationBase,
                        ConfigurableBase<DummyCalibrationConfig>> {
  public:
    DummyCalibration() = default;
    ~DummyCalibration() = default;

  protected:
    void onInit() override {}
    void onPause() override {}
    void onResume() override {}
    void onReset() override {}
    void onShutdown() override {}

    void onCalibrate(core::EyeData &data) override {
        float ox = getConfig().offset_x;
        float oy = getConfig().offset_y;
        data.left.gaze.raw.x += ox;
        data.left.gaze.raw.y += oy;
        data.right.gaze.raw.x += ox;
        data.right.gaze.raw.y += oy;
    }

    void onCalibrationPointsUpdated(
        std::span<const CalibrationPoint> points) override {
        std::println("DummyCalibration: Received {} calibration points",
                     points.size());
    }
};

} // namespace reyer::plugin

REYER_PLUGIN_ENTRY(reyer::plugin::DummyCalibration, "Dummy Calibration", 1)