#include "reyer/core/core.hpp"
#include "reyer/core/utils.hpp"
#include "reyer/plugin/interfaces.hpp"
#include "reyer/plugin/loader.hpp"
#include <cmath>
#include <raylib.h>
#include <spdlog/spdlog.h>

namespace reyer::plugin {

struct SimpleCalibrationConfig {
    float stimulus_size_arcmin = 10.0f;
    vec2<float> grid_spacing_degrees = {3.0f, 3.0f};
    int num_samples = 30;
    float max_std_dev = 100.0f;
};

struct RingBufferStats {
    void resize(int capacity) {
        buf_.assign(static_cast<size_t>(capacity), {0.0f, 0.0f});
        head_ = 0;
        count_ = 0;
        sum_ = {};
        sum_sq_ = {};
    }

    void push(vec2<float> v) {
        size_t cap = buf_.size();
        if (count_ == cap) {
            auto &old = buf_[head_];
            sum_.x -= old.x;
            sum_.y -= old.y;
            sum_sq_.x -= old.x * old.x;
            sum_sq_.y -= old.y * old.y;
        }
        buf_[head_] = v;
        sum_.x += v.x;
        sum_.y += v.y;
        sum_sq_.x += v.x * v.x;
        sum_sq_.y += v.y * v.y;
        head_ = (head_ + 1) % cap;
        if (count_ < cap)
            ++count_;
    }

    vec2<float> mean() const {
        float inv = 1.0f / static_cast<float>(count_);
        return {sum_.x * inv, sum_.y * inv};
    }

    float std_dev() const {
        float inv = 1.0f / static_cast<float>(count_);
        auto m = mean();
        float var_x = sum_sq_.x * inv - m.x * m.x;
        float var_y = sum_sq_.y * inv - m.y * m.y;
        return std::sqrt(var_x + var_y);
    }

    size_t count() const { return count_; }

  private:
    std::vector<vec2<float>> buf_;
    size_t head_ = 0;
    size_t count_ = 0;
    vec2<float> sum_{};
    vec2<float> sum_sq_{};
};

class SimpleCalibration : public RenderPluginBase<SimpleCalibrationConfig> {
  public:
    SimpleCalibration() = default;
    ~SimpleCalibration() = default;

  protected:
    void onInit() override {
        current_point_ = 0;
        collected_points_.clear();
        ring_left_.resize(getConfig().num_samples);
        ring_right_.resize(getConfig().num_samples);

        // 3x3 grid positions in degrees relative to center
        float dx = getConfig().grid_spacing_degrees.x;
        float dy = getConfig().grid_spacing_degrees.y;
        float xs[3] = {-dx, 0.0f, dx};
        float ys[3] = {-dy, 0.0f, dy};

        grid_points_.clear();
        for (int row = 0; row < 3; ++row) {
            for (int col = 0; col < 3; ++col) {
                grid_points_.push_back({xs[col], ys[row]});
                spdlog::info("Calibration point {} (deg): ({}, {})",
                             row * 3 + col + 1, xs[col], ys[row]);
            }
        }
    }

    void onPause() override {}
    void onResume() override {}
    void onReset() override {
        spdlog::info("Calibration reset, starting over");
        current_point_ = 0;
        collected_points_.clear();
        ring_left_.resize(getConfig().num_samples);
        ring_right_.resize(getConfig().num_samples);
    }
    void onShutdown() override {}

    void onRender() override {
        if (current_point_ >= 9) {
            // All points collected — push and finish
            pushCalibrationPoints(std::move(collected_points_));
            current_point_ = 0;
            collected_points_.clear();
            is_calibrated_ = true;
            return;
        }

        if (is_calibrated_) {
            if (IsGamepadButtonPressed(0, GAMEPAD_BUTTON_RIGHT_FACE_RIGHT)) {
                endTask();
                return;
            }
            DrawCircleV(eye_left, 20, RED);
        }

        auto &target_deg = grid_points_[current_point_];

        // Convert degrees to pixel coordinates for rendering
        vec2<float> target_px = degreesToPixels(target_deg);

        float r = getConfig().stimulus_size_arcmin / 60.0f *
                  getRenderContext().ppd_x / 2.0f;

        // Draw the target point
        DrawCircleV({target_px.x, target_px.y}, r, BLACK);
        DrawCircleV({target_px.x, target_px.y}, r * 0.3f, WHITE);
        // DrawCircleV(eye_right, 20, BLUE);

        // Draw progress text
        const char *text = TextFormat("Point %d / 9  —  Press N to confirm",
                                      current_point_ + 1);
        int font_size = 20;
        int tw = MeasureText(text, font_size);
        DrawText(text, (GetScreenWidth() - tw) / 2, GetScreenHeight() - 40,
                 font_size, WHITE);

        if (IsKeyPressed(KEY_N) ||
            IsGamepadButtonPressed(0, GAMEPAD_BUTTON_RIGHT_TRIGGER_1)) {
            int required = getConfig().num_samples;
            if (static_cast<int>(ring_left_.count()) < required ||
                static_cast<int>(ring_right_.count()) < required) {
                spdlog::warn(
                    "Not enough samples: left={}, right={}, required={}",
                    ring_left_.count(), ring_right_.count(), required);
            } else if (ring_left_.std_dev() >= getConfig().max_std_dev ||
                       ring_right_.std_dev() >= getConfig().max_std_dev) {
                spdlog::warn(
                    "Std dev too high: left={:.1f}, right={:.1f}, max={:.1f}",
                    ring_left_.std_dev(), ring_right_.std_dev(),
                    getConfig().max_std_dev);
            } else {
                vec2<float> mean_left = ring_left_.mean();
                vec2<float> mean_right = ring_right_.mean();

                spdlog::info("Control point (deg): {}, {}", target_deg.x,
                             target_deg.y);
                spdlog::info("Measured left: {}, {} (std={:.1f})", mean_left.x,
                             mean_left.y, ring_left_.std_dev());
                spdlog::info("Measured right: {}, {} (std={:.1f})",
                             mean_right.x, mean_right.y, ring_right_.std_dev());

                collected_points_.push_back(CalibrationPoint{
                    .control_point = target_deg,
                    .measured_point = mean_left,
                    .eye = Eye::Left,
                });
                collected_points_.push_back(CalibrationPoint{
                    .control_point = target_deg,
                    .measured_point = mean_right,
                    .eye = Eye::Right,
                });

                ring_left_.resize(getConfig().num_samples);
                ring_right_.resize(getConfig().num_samples);
                ++current_point_;
            }
        }
    }

    void onConsume(const core::EyeData &data) override {
        ring_left_.push({data.left.dpi.p1.x - data.left.dpi.p4.x,
                         data.left.dpi.p1.y - data.left.dpi.p4.y});
        ring_right_.push({data.right.dpi.p1.x - data.right.dpi.p4.x,
                          data.right.dpi.p1.y - data.right.dpi.p4.y});

        auto left = degreesToPixels(data.left.gaze.raw);
        auto right = degreesToPixels(data.right.gaze.raw);
        eye_left.x = left.x;
        eye_left.y = left.y;
        eye_right.x = right.x;
        eye_right.y = right.y;
    }

    vec2<float> degreesToPixels(vec2<float> deg) const {
        float cx = static_cast<float>(GetScreenWidth()) / 2.0f;
        float cy = static_cast<float>(GetScreenHeight()) / 2.0f;
        return {cx + deg.x * static_cast<float>(getRenderContext().ppd_x),
                cy + deg.y * static_cast<float>(getRenderContext().ppd_y)};
    }

  private:
    int current_point_ = 0;
    RingBufferStats ring_left_;
    RingBufferStats ring_right_;
    std::vector<vec2<float>> grid_points_;
    std::vector<CalibrationPoint> collected_points_;

    bool is_calibrated_ = false;

    Vector2 eye_left;
    Vector2 eye_right;
};

} // namespace reyer::plugin

REYER_PLUGIN_ENTRY(reyer::plugin::SimpleCalibration, "Simple Calibration",
                   "Soma Mizobuchi", "A simple calibration plugin.", 1)
