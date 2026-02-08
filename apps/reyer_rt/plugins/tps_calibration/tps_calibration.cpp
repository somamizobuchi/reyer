#include "reyer/core/core.hpp"
#include "reyer/plugin/interfaces.hpp"
#include "reyer/plugin/loader.hpp"
#include <spdlog/spdlog.h>
#include "tps.hpp"
#include <span>

namespace reyer::plugin {

struct TPSCalibrationConfig {
    float offset_x = 0.0f;
    float offset_y = 0.0f;
};

class TPSCalibration
    : public PluginBase<CalibrationBase,
                        ConfigurableBase<TPSCalibrationConfig>> {
  public:
    TPSCalibration() = default;
    ~TPSCalibration() = default;

  protected:
    void onInit() override {
        calibration_right_.Init();
        calibration_left_.Init();
    }

    void onPause() override {}
    void onResume() override {}
    void onReset() override {}
    void onShutdown() override {}

    void onCalibrate(core::EyeData &data) override {
        if (calibration_right_.isCalibrated()) {
            vec2<float> dp{data.right.dpi.p1.x - data.right.dpi.p4.x, data.right.dpi.p1.y - data.right.dpi.p4.y};
            data.right.gaze.raw = calibration_right_.calibrate(dp);
            // spdlog::info("Right: {}, {}", data.right.gaze.raw.x, data.right.gaze.raw.y);
        }
        if (calibration_left_.isCalibrated()) {
            vec2<float> dp{data.left.dpi.p1.x - data.left.dpi.p4.x, data.left.dpi.p1.y - data.left.dpi.p4.y};
            data.left.gaze.raw = calibration_left_.calibrate(dp);
            // spdlog::info("Left: {}, {}", data.left.gaze.raw.x, data.left.gaze.raw.y);
        }
    }

    void onCalibrationPointsUpdated(
        std::span<const CalibrationPoint> points) override {
            std::vector<CalibrationPoint> leftEyePoints, rightEyePoints;
            for (const auto &point : points)
            {
                if (point.eye == Eye::Left)
                {
                    leftEyePoints.push_back(point);
                } else {
                    rightEyePoints.push_back(point);
                }
            }
            std::span<const CalibrationPoint> left(leftEyePoints);
            std::span<const CalibrationPoint> right(rightEyePoints);

            calibration_right_.setPoints(right);
            calibration_left_.setPoints(left);
    }

    calibration::Calibration calibration_right_;
    calibration::Calibration calibration_left_;
};

} // namespace reyer::plugin

REYER_PLUGIN_ENTRY(reyer::plugin::TPSCalibration, "TPS Calibration", 1)