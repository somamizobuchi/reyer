#pragma once
#include "reyer/plugin/interfaces.hpp"
#include <memory>
#include <vector>

namespace reyer::plugin {

// The pipeline holds shared ownership of everything it references so that a
// source/stage/sink cannot be destroyed by another thread while processData()
// still holds it. Callers must serialize processData() against mutation (the
// PipelineManager does this via its mutex).
template <typename T> class Pipeline {
  public:
    void setSource(std::shared_ptr<ISource<T>> source) {
        source_ = std::move(source);
    }

    void addStage(std::shared_ptr<IStage<T>> stage) {
        stages_.push_back(std::move(stage));
    }

    void addSink(std::shared_ptr<ISink<T>> sink) {
        sinks_.push_back(std::move(sink));
    }

    virtual void processData(T data) {
        for (auto &stage : stages_)
            stage->process(data);

        for (auto &sink : sinks_)
            sink->consume(data);
    }

    ISource<T> *getSourceInterface() const { return source_.get(); }

    virtual void clear() {
        if (source_)
            source_->cancel();
        source_ = nullptr;
        stages_.clear();
        sinks_.clear();
    }

    void clearSinks() { sinks_.clear(); }

    bool hasSource() const { return source_ != nullptr; }
    size_t stageCount() const { return stages_.size(); }
    size_t sinkCount() const { return sinks_.size(); }

    virtual ~Pipeline() = default;

  private:
    std::shared_ptr<ISource<T>> source_;
    std::vector<std::shared_ptr<IStage<T>>> stages_;
    std::vector<std::shared_ptr<ISink<T>>> sinks_;
};

class EyeDataPipeline : public Pipeline<core::EyeData> {
  public:
    void setCalibration(std::shared_ptr<ICalibration> calibration) {
        calibration_ = std::move(calibration);
    }

    ICalibration *getCalibration() const { return calibration_.get(); }

    void processData(core::EyeData data) override {
        if (calibration_)
            calibration_->calibrate(&data);

        Pipeline::processData(data);
    }

    void clear() override {
        calibration_.reset();
        Pipeline::clear();
    }

  private:
    std::shared_ptr<ICalibration> calibration_;
};

using EyePipeline = EyeDataPipeline;

} // namespace reyer::plugin
