#pragma once

#include "reyer_rt/threading/thread.hpp"
#include <memory>
#include <mutex>
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

        source_ = source;
        stages_ = std::move(stages);

        if (auto src = source_.asShared<reyer::plugin::IEyeSource>()) {
            pipeline_.setSource(std::move(src));
            spdlog::info("Pipeline: configured source '{}'", source_.getName());
        }

        for (auto &stage : stages_) {
            if (auto stg = stage.asShared<reyer::plugin::IEyeStage>()) {
                pipeline_.addStage(std::move(stg));
                spdlog::info("Pipeline: configured stage '{}'",
                             stage.getName());
            }
        }

        // Initialise the new source/stages on the pipeline thread, not here.
        postTask([this] {
            std::lock_guard<std::mutex> lock(mutex_);
            initPlugins_();
            spdlog::info("Pipeline: plugins initialized on pipeline thread");
        });
    }

    void SetCalibration(std::shared_ptr<reyer::plugin::ICalibration> cal) {
        std::lock_guard<std::mutex> lock(mutex_);
        pipeline_.setCalibration(std::move(cal));
        spdlog::info("Pipeline: calibration updated");
    }

    void ReplaceSink(reyer::plugin::Plugin sink) {
        std::lock_guard<std::mutex> lock(mutex_);
        pipeline_.clearSinks();

        if (auto snk = sink.asShared<reyer::plugin::IEyeSink>()) {
            pipeline_.addSink(std::move(snk));
            spdlog::info("Pipeline: replaced sink with '{}'", sink.getName());
        }
    }

    void AddSink(std::shared_ptr<reyer::plugin::ISink<reyer::core::EyeData>> sink) {
        std::lock_guard<std::mutex> lock(mutex_);
        pipeline_.addSink(std::move(sink));
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
    // init/shutdown are guarded by initialized_ so they are idempotent: a
    // redundant posted init (e.g. two Configure() calls in quick succession)
    // is a no-op, and plugins are never double-init'd or double-shutdown.
    void initPlugins_() {
        if (initialized_)
            return;
        if (source_)
            source_->init();
        for (auto &stage : stages_)
            stage->init();
        initialized_ = true;
    }

    void shutdownPlugins_() {
        if (!initialized_)
            return;
        for (auto &stage : stages_)
            stage->shutdown();
        if (source_)
            source_->shutdown();
        initialized_ = false;
    }

    reyer::plugin::EyePipeline pipeline_;

    reyer::plugin::Plugin source_;
    std::vector<reyer::plugin::Plugin> stages_;

    std::mutex mutex_;
    bool initialized_{false}; // guarded by mutex_
};

} // namespace reyer_rt::managers
