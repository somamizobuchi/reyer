#include "reyer/core/core.hpp"
#include "reyer/plugin/interfaces.hpp"
#include "reyer/plugin/loader.hpp"
#include <raylib.h>
#include <spdlog/spdlog.h>
#include <vector>

namespace reyer::plugin {

struct SimpleCalibrationConfig {
    float point_radius = 10.0f;
    float margin_ratio = 0.1f; // fraction of screen width/height for margin
};

class SimpleCalibration : public RenderPluginBase<SimpleCalibrationConfig> {
  public:
    SimpleCalibration() = default;
    ~SimpleCalibration() = default;

  protected:
    void onInit() override {
        current_point_ = 0;
        collected_points_.clear();
        sample_sum_left_ = {};
        sample_sum_right_ = {};
        sample_count_ = 0;

        float w = static_cast<float>(GetScreenWidth());
        float h = static_cast<float>(GetScreenHeight());
        float mx = w * getConfig().margin_ratio;
        float my = h * getConfig().margin_ratio;

        // 3x3 grid positions
        float xs[3] = {mx, w * 0.5f, w - mx};
        float ys[3] = {my, h * 0.5f, h - my};

        grid_points_.clear();
        for (int row = 0; row < 3; ++row)
            for (int col = 0; col < 3; ++col)
                grid_points_.push_back({xs[col], ys[row]});
    }

    void onPause() override {}
    void onResume() override {}
    void onReset() override {
        spdlog::info("Calibration reset, starting over");
        current_point_ = 0;
        collected_points_.clear();
        sample_sum_left_ = {};
        sample_sum_right_ = {};
        sample_count_ = 0;
    }
    void onShutdown() override {}

    void onRender() override {
        if (current_point_ >= 9) {
            // All points collected — push and finish
            pushCalibrationPoints(std::move(collected_points_));
            endTask();
            return;
        }

        auto &target = grid_points_[current_point_];
        float r = getConfig().point_radius;

        // Draw the target point
        DrawCircleV({target.x, target.y}, r, RED);
        DrawCircleV({target.x, target.y}, r * 0.3f, WHITE);

        // Draw progress text
        const char *text = TextFormat("Point %d / 9  —  Press N to confirm",
                                      current_point_ + 1);
        int font_size = 20;
        int tw = MeasureText(text, font_size);
        DrawText(text, (GetScreenWidth() - tw) / 2, GetScreenHeight() - 40,
                 font_size, WHITE);

        if (IsKeyPressed(KEY_N)) {
            // Record averaged measurement for this point
            if (sample_count_ > 0) {
                float inv = 1.0f / static_cast<float>(sample_count_);
                vec2<float> mean_left{sample_sum_left_.x * inv,
                                      sample_sum_left_.y * inv};
                vec2<float> mean_right{sample_sum_right_.x * inv,
                                       sample_sum_right_.y * inv};

                collected_points_.push_back(CalibrationPoint{
                    .control_point = target,
                    .measured_point = mean_left,
                    .eye = Eye::Left,
                });
                collected_points_.push_back(CalibrationPoint{
                    .control_point = target,
                    .measured_point = mean_right,
                    .eye = Eye::Right,
                });
            }

            // Reset accumulators for next point
            sample_sum_left_ = {};
            sample_sum_right_ = {};
            sample_count_ = 0;
            ++current_point_;
        }
    }

    void onConsume(const core::EyeData &data) override {
        sample_sum_left_.x += data.left.gaze.raw.x;
        sample_sum_left_.y += data.left.gaze.raw.y;
        sample_sum_right_.x += data.right.gaze.raw.x;
        sample_sum_right_.y += data.right.gaze.raw.y;
        ++sample_count_;
    }

  private:
    int current_point_ = 0;
    uint64_t sample_count_ = 0;
    vec2<float> sample_sum_left_{};
    vec2<float> sample_sum_right_{};
    std::vector<vec2<float>> grid_points_;
    std::vector<CalibrationPoint> collected_points_;
};

} // namespace reyer::plugin

REYER_PLUGIN_ENTRY(reyer::plugin::SimpleCalibration, "Simple Calibration", 1)
