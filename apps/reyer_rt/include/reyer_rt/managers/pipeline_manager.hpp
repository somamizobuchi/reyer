#pragma once

#include "reyer_rt/pipeline/pipeline.hpp"
#include "reyer_rt/threading/thread.hpp"
#include <reyer/core/core.hpp>
#include <reyer/plugin/loader.hpp>

namespace reyer_rt::managers {

class PipelineManager : public threading::Thread<PipelineManager> {
  public:
    PipelineManager() = default;

    ~PipelineManager() = default;

    void Init() {}

    void Run() {
        while (!get_stop_token().stop_requested()) {
            for (auto &stage : source_) {
                reyer::core::EyeData data;
                stage->process(&data, 1);
            }
        }
    }

    void Shutdown() {}

  private:
    std::vector<reyer::plugin::IEyeStage *> source_;
    reyer::plugin::Plugin render_;
};
} // namespace reyer_rt::managers
