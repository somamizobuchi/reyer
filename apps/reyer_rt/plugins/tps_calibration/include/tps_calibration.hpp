#pragma once
#include "tps.hpp"
#include <reyer/plugin/interfaces.hpp>

namespace reyer::plugin {

class TPSCalibration : public CalibrationBase {
  public:
    TPSCalibration() {
        calibration_right_.Init();
        calibration_left_.Init();
    }

  protected:
    void onCalibrate(core::EyeData &data) override {
        if (calibration_right_.isCalibrated()) {
            vec2<float> dp{data.right.dpi.p1.x - data.right.dpi.p4.x,
                           data.right.dpi.p1.y - data.right.dpi.p4.y};
            data.right.gaze.raw = calibration_right_.calibrate(dp);
        }
        if (calibration_left_.isCalibrated()) {
            vec2<float> dp{data.left.dpi.p1.x - data.left.dpi.p4.x,
                           data.left.dpi.p1.y - data.left.dpi.p4.y};
            data.left.gaze.raw = calibration_left_.calibrate(dp);
        }
    }

    void onCalibrationPointsUpdated(
        std::span<const CalibrationPoint> points) override {
        std::vector<CalibrationPoint> leftEyePoints, rightEyePoints;
        for (const auto &point : points) {
            if (point.eye == Eye::Left)
                leftEyePoints.push_back(point);
            else
                rightEyePoints.push_back(point);
        }
        calibration_right_.setPoints(
            std::span<const CalibrationPoint>(rightEyePoints));
        calibration_left_.setPoints(
            std::span<const CalibrationPoint>(leftEyePoints));
    }

  private:
    calibration::Calibration calibration_right_;
    calibration::Calibration calibration_left_;
};

} // namespace reyer::plugin
