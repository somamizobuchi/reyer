#include "foaw.hpp"
#include "reyer/core/core.hpp"
#include "reyer/plugin/interfaces.hpp"
#include "reyer/plugin/loader.hpp"

namespace reyer::plugin {

struct FoawVelocityFilterConfig {
    size_t maxWindowSize = 50;  // Maximum number of samples to look back (n)
    double samplingTime = 0.01; // T in seconds
    double noiseBound = 0.5;    // delta in the FOAW algorithm
};

class FoawVelocityFilter
    : public PluginBase<EyeStageBase,
                        ConfigurableBase<FoawVelocityFilterConfig>> {
  public:
    FoawVelocityFilter() = default;

    ~FoawVelocityFilter() = default;

  protected:
    void onInit() override {
        foaw_left_x = std::make_unique<FOAW>(getConfig().maxWindowSize,
                                             getConfig().samplingTime,
                                             getConfig().noiseBound);
        foaw_left_y = std::make_unique<FOAW>(getConfig().maxWindowSize,
                                             getConfig().samplingTime,
                                             getConfig().noiseBound);
        foaw_right_x = std::make_unique<FOAW>(getConfig().maxWindowSize,
                                              getConfig().samplingTime,
                                              getConfig().noiseBound);
        foaw_right_y = std::make_unique<FOAW>(getConfig().maxWindowSize,
                                              getConfig().samplingTime,
                                              getConfig().noiseBound);
    }

    void onPause() override {}
    void onResume() override {}
    void onReset() override {}
    void onShutdown() override {}

    void onProcess(core::EyeData &data) override {
        double left_x = data.left.gaze.raw.x;
        double left_y = data.left.gaze.raw.y;
        double right_x = data.right.gaze.raw.x;
        double right_y = data.right.gaze.raw.y;

        double left_vx = foaw_left_x->update(left_x);
        double left_vy = foaw_left_y->update(left_y);
        double right_vx = foaw_right_x->update(right_x);
        double right_vy = foaw_right_y->update(right_y);

        data.left.gaze.velocity.x = left_vx;
        data.left.gaze.velocity.y = left_vy;
        data.right.gaze.velocity.x = right_vx;
        data.right.gaze.velocity.y = right_vy;
    }

  private:
    std::unique_ptr<FOAW> foaw_left_x;
    std::unique_ptr<FOAW> foaw_left_y;
    std::unique_ptr<FOAW> foaw_right_x;
    std::unique_ptr<FOAW> foaw_right_y;
};

} // namespace reyer::plugin

REYER_PLUGIN_ENTRY(reyer::plugin::FoawVelocityFilter, "FOAW Velocity filter",
                   "Soma Mizobuchi", "FOAW Velocity Filter", 1);
