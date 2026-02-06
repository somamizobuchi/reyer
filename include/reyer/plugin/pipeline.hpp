#pragma once
#include "reyer/plugin/interfaces.hpp"
#include <vector>

namespace reyer::plugin {

template <typename T> class Pipeline {
  public:
    void setSource(IPlugin *plugin, ISource<T> *source) {
        source_ = {plugin, source};
    }

    void addStage(IPlugin *plugin, IStage<T> *stage) {
        stages_.push_back({plugin, stage});
    }

    void setCalibration(IPlugin *plugin, ICalibration *calibration) {
        calibration_ = {plugin, calibration};
    }

    void setFilter(IPlugin *plugin, IFilter *filter) {
        filter_ = {plugin, filter};
    }

    void addSink(IPlugin *plugin, ISink<T> *sink) {
        sinks_.push_back({plugin, sink});
    }

    void init() {
        if (source_.plugin)
            source_.plugin->init();
        if (calibration_.plugin)
            calibration_.plugin->init();
        if (filter_.plugin)
            filter_.plugin->init();
        for (auto &[plugin, _] : stages_)
            plugin->init();
        for (auto &[plugin, _] : sinks_)
            plugin->init();
        initialized_ = true;
    }

    void shutdown() {
        for (auto &[plugin, _] : sinks_)
            plugin->shutdown();
        for (auto &[plugin, _] : stages_)
            plugin->shutdown();
        if (filter_.plugin)
            filter_.plugin->shutdown();
        if (calibration_.plugin)
            calibration_.plugin->shutdown();
        if (source_.plugin)
            source_.plugin->shutdown();
        initialized_ = false;
    }

    void processData(T data) {
        if (calibration_.iface)
            calibration_.iface->calibrate(&data);

        if (filter_.iface)
            filter_.iface->filter(&data);

        for (auto &[_, stage] : stages_)
            stage->process(data);

        for (auto &[_, sink] : sinks_)
            sink->consume(data);
    }

    ISource<T> *getSourceInterface() const { return source_.iface; }
    ICalibration *getCalibration() const { return calibration_.iface; }
    IFilter *getFilter() const { return filter_.iface; }

    void clear() {
        if (source_.iface)
            source_.iface->cancel();
        source_ = {};
        calibration_ = {};
        filter_ = {};
        stages_.clear();
        sinks_.clear();
        initialized_ = false;
    }

    void clearSinks() { sinks_.clear(); }

    bool hasSource() const { return source_.iface != nullptr; }
    size_t stageCount() const { return stages_.size(); }
    size_t sinkCount() const { return sinks_.size(); }
    bool isInitialized() const { return initialized_; }

  private:
    template <typename I> struct PluginInterface {
        IPlugin *plugin = nullptr;
        I *iface = nullptr;
    };

    PluginInterface<ISource<T>> source_;
    PluginInterface<ICalibration> calibration_;
    PluginInterface<IFilter> filter_;
    std::vector<PluginInterface<IStage<T>>> stages_;
    std::vector<PluginInterface<ISink<T>>> sinks_;
    bool initialized_ = false;
};

using EyePipeline = Pipeline<core::EyeData>;

} // namespace reyer::plugin
