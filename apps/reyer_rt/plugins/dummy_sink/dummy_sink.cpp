#include "reyer/core/core.hpp"
#include "reyer/plugin/interfaces.hpp"
#include "reyer/plugin/loader.hpp"
#include <cstdio>

namespace reyer::plugin {

struct DummySinkConfig {
    bool verbose = false;
};

class DummySink
    : public PluginBase<EyeSinkBase, ConfigurableBase<DummySinkConfig>> {
  public:
    DummySink() = default;
    ~DummySink() = default;

  protected:
    void onInit() override {}
    void onPause() override {}
    void onResume() override {}
    void onReset() override { sample_count_ = 0; }
    void onShutdown() override {}

    void onConsume(const core::EyeData &data) override {
        ++sample_count_;
    }

  private:
    uint64_t sample_count_ = 0;
};

} // namespace reyer::plugin

REYER_PLUGIN_ENTRY(reyer::plugin::DummySink, "Dummy Sink", 1)
