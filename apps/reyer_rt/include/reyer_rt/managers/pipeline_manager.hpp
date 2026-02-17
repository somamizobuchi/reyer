#pragma once

#include "reyer_rt/threading/thread.hpp"
#include <atomic>
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
        std::lock_guard<std::mutex> lock(mutex_);
        initPlugins_();
        spdlog::info("Pipeline: initialized with {} stage(s)",
                     pipeline_.stageCount());
    }

    void Run() {
        if (needs_init_.exchange(false, std::memory_order_acq_rel)) {
            std::lock_guard<std::mutex> lock(mutex_);
            initPlugins_();
            spdlog::info("Pipeline: plugins initialized on pipeline thread");
        }

        reyer::plugin::ISource<reyer::core::EyeData> *source = nullptr;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            source = pipeline_.getSourceInterface();
        }
        if (!source) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            return;
        }

        reyer::core::EyeData sample{};
        if (!source->waitForData(sample, get_stop_token()))
            return;

        std::lock_guard<std::mutex> lock(mutex_);
        pipeline_.processData(sample);
    }

    void Shutdown() {
        std::lock_guard<std::mutex> lock(mutex_);
        pipeline_.clearSinks();
        shutdownPlugins_();
        pipeline_.clear();
    }

    void Configure(reyer::plugin::Plugin source,
                   std::optional<reyer::plugin::Plugin> calibration,
                   std::vector<reyer::plugin::Plugin> stages) {
        // Cancel old source outside lock to wake blocked waitForData
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (auto *old_src = pipeline_.getSourceInterface())
                old_src->cancel();
        }
        std::lock_guard<std::mutex> lock(mutex_);
        shutdownPlugins_();
        pipeline_.clear();

        // Store plugins for lifecycle management
        source_ = source;
        calibration_ = calibration;
        stages_ = std::move(stages);

        if (auto *src = source_.as<reyer::plugin::IEyeSource>()) {
            pipeline_.setSource(src);
            spdlog::info("Pipeline: configured source '{}'", source_.getName());
        }

        if (calibration_) {
            if (auto *cal = calibration_->as<reyer::plugin::ICalibration>()) {
                pipeline_.setCalibration(cal);
                spdlog::info("Pipeline: configured calibration '{}'",
                             calibration_->getName());
            }
        }

        for (auto &stage : stages_) {
            if (auto *stg = stage.as<reyer::plugin::IEyeStage>()) {
                pipeline_.addStage(stg);
                spdlog::info("Pipeline: configured stage '{}'",
                             stage.getName());
            }
        }

        needs_init_.store(true, std::memory_order_release);
    }

    void ReplaceSink(reyer::plugin::Plugin sink) {
        std::lock_guard<std::mutex> lock(mutex_);
        pipeline_.clearSinks();

        if (auto *snk = sink.as<reyer::plugin::IEyeSink>()) {
            pipeline_.addSink(snk);
            spdlog::info("Pipeline: replaced sink with '{}'", sink.getName());
        }
    }

    void AddSink(reyer::plugin::ISink<reyer::core::EyeData> *sink) {
        std::lock_guard<std::mutex> lock(mutex_);
        pipeline_.addSink(sink);
    }

    void RemoveSink() {
        std::lock_guard<std::mutex> lock(mutex_);
        pipeline_.clearSinks();
        spdlog::info("Pipeline: removed sink");
    }

    void ClearPipeline() {
        std::lock_guard<std::mutex> lock(mutex_);
        pipeline_.clearSinks();
        shutdownPlugins_();
        pipeline_.clear();
        spdlog::info("Pipeline: cleared");
    }

    reyer::plugin::EyePipeline &pipeline() { return pipeline_; }

  private:
    void initPlugins_() {
        if (source_)
            source_->init();
        if (calibration_)
            (*calibration_)->init();
        for (auto &stage : stages_)
            stage->init();
    }

    void shutdownPlugins_() {
        for (auto &stage : stages_)
            stage->shutdown();
        if (calibration_)
            (*calibration_)->shutdown();
        if (source_)
            source_->shutdown();
    }

    reyer::plugin::EyePipeline pipeline_;

    reyer::plugin::Plugin source_;
    std::optional<reyer::plugin::Plugin> calibration_;
    std::vector<reyer::plugin::Plugin> stages_;

    std::mutex mutex_;
    std::atomic<bool> needs_init_{false};
};

} // namespace reyer_rt::managers
