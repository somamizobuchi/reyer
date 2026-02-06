#pragma once

#include "reyer_rt/threading/thread.hpp"
#include <mutex>
#include <optional>
#include <reyer/core/core.hpp>
#include <reyer/plugin/loader.hpp>
#include <reyer/plugin/pipeline.hpp>
#include <spdlog/spdlog.h>
#include <thread>
#include <vector>

namespace reyer_rt::managers {

class PipelineManager : public threading::Thread<PipelineManager> {
  public:
    PipelineManager() = default;
    ~PipelineManager() = default;

    void Init() {
        // No-op: pipeline is configured later via Configure()
    }

    void Run() {
        while (!get_stop_token().stop_requested()) {
            reyer::plugin::ISource<reyer::core::EyeData> *source = nullptr;
            {
                std::lock_guard<std::mutex> lock(mutex_);
                source = pipeline_.getSourceInterface();
            }
            if (!source) {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
                continue;
            }

            reyer::core::EyeData sample{};
            if (!source->waitForData(sample, get_stop_token()))
                continue;

            std::lock_guard<std::mutex> lock(mutex_);
            pipeline_.processData(sample);
        }
    }

    void Shutdown() {
        std::lock_guard<std::mutex> lock(mutex_);
        pipeline_.clearSinks();
        pipeline_.shutdown();
        pipeline_.clear();
    }

    void Configure(reyer::plugin::Plugin source,
                   std::optional<reyer::plugin::Plugin> calibration,
                   std::optional<reyer::plugin::Plugin> filter,
                   std::vector<reyer::plugin::Plugin> stages) {
        // Cancel old source outside lock to wake blocked waitForData
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (auto *old_src = pipeline_.getSourceInterface())
                old_src->cancel();
        }
        std::lock_guard<std::mutex> lock(mutex_);
        pipeline_.shutdown();
        pipeline_.clear();

        if (auto *src = source.as<reyer::plugin::IEyeSource>()) {
            pipeline_.setSource(source.get(), src);
            spdlog::info("Pipeline: configured source '{}'", source.getName());
        }

        if (calibration) {
            if (auto *cal = calibration->as<reyer::plugin::ICalibration>()) {
                pipeline_.setCalibration(calibration->get(), cal);
                spdlog::info("Pipeline: configured calibration '{}'",
                             calibration->getName());
            }
        }

        if (filter) {
            if (auto *flt = filter->as<reyer::plugin::IFilter>()) {
                pipeline_.setFilter(filter->get(), flt);
                spdlog::info("Pipeline: configured filter '{}'",
                             filter->getName());
            }
        }

        for (auto &stage : stages) {
            if (auto *stg = stage.as<reyer::plugin::IEyeStage>()) {
                pipeline_.addStage(stage.get(), stg);
                spdlog::info("Pipeline: configured stage '{}'",
                             stage.getName());
            }
        }

        pipeline_.init();
        spdlog::info("Pipeline: initialized with {} stage(s)",
                     pipeline_.stageCount());
    }

    void ReplaceSink(reyer::plugin::Plugin sink) {
        std::lock_guard<std::mutex> lock(mutex_);
        pipeline_.clearSinks();

        if (auto *snk = sink.as<reyer::plugin::IEyeSink>()) {
            pipeline_.addSink(sink.get(), snk);
            spdlog::info("Pipeline: replaced sink with '{}'", sink.getName());
        }
    }

    void RemoveSink() {
        std::lock_guard<std::mutex> lock(mutex_);
        pipeline_.clearSinks();
        spdlog::info("Pipeline: removed sink");
    }

    void ClearPipeline() {
        std::lock_guard<std::mutex> lock(mutex_);
        pipeline_.clearSinks();
        pipeline_.shutdown();
        pipeline_.clear();
        spdlog::info("Pipeline: cleared");
    }

    reyer::plugin::EyePipeline &pipeline() { return pipeline_; }

  private:
    reyer::plugin::EyePipeline pipeline_;
    std::mutex mutex_;
};

} // namespace reyer_rt::managers
