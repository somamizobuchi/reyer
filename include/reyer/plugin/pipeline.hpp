#pragma once
#include "reyer/plugin/interfaces.hpp"
#include <memory>
#include <vector>

namespace reyer::plugin {

template <typename T> class Pipeline {
  public:
    void setSource(ISource<T> *source) { source_ = source; }

    void addStage(IStage<T> *stage) { stages_.push_back(stage); }

    void addSink(ISink<T> *sink) { sinks_.push_back(sink); }

    virtual void processData(T data) {
        for (auto *stage : stages_)
            stage->process(data);

        for (auto *sink : sinks_)
            sink->consume(data);
    }

    ISource<T> *getSourceInterface() const { return source_; }

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
    ISource<T> *source_ = nullptr;
    std::vector<IStage<T> *> stages_;
    std::vector<ISink<T> *> sinks_;
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
