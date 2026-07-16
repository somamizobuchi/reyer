#pragma once
#include "reyer/core/core.hpp"
#include "reyer/core/queue.hpp"
#include "reyer/core/thread.hpp"
#include "reyer/core/utils.hpp"
#include <glaze/core/context.hpp>
#include <glaze/json/read.hpp>
#include <glaze/json/schema.hpp>
#include <glaze/util/expected.hpp>
#include <memory>
#include <mutex>
#include <string>
#include <type_traits>

#define REYER_DEFINE_INTERFACE_ID(name)                                        \
    static constexpr InterfaceID iid = {reyer::core::hash_string(#name)};

#define REYER_REGISTER_TYPED_INTERFACE(Interface, Type, Alias)                 \
    template <>                                                                \
    inline const InterfaceID Interface<Type>::iid = {                          \
        reyer::core::hash_string(#Interface "<" #Type ">")};                   \
    using Alias = Interface<Type>;

namespace reyer::plugin {

struct InterfaceID {
    uint64_t value;

    constexpr bool operator==(const InterfaceID &other) const {
        return value == other.value;
    }
};

struct ILifecycle {
    virtual void init() = 0;
    virtual void shutdown() = 0;
    virtual void reset() = 0;
    virtual ~ILifecycle() = default;
};

struct IConfigurable {
    REYER_DEFINE_INTERFACE_ID(IConfigurable)
    virtual const char *getConfigSchema() = 0;
    virtual const char *getDefaultConfig() = 0;
    virtual void setConfigStr(const char *config_str) = 0;
    virtual ~IConfigurable() = default;
};

template <typename TConfig>
class ConfigurableBase : public virtual IConfigurable {
  public:
    const char *getConfigSchema() override {
        auto err = glz::write_json_schema<TConfig>(schema_buffer_);
        if (err) {
            return "{}";
        }
        return schema_buffer_.c_str();
    }

    const char *getDefaultConfig() override {
        auto err = glz::write_json<TConfig>(TConfig{}, default_config_buffer_);
        if (err) {
            return "{}";
        }
        return default_config_buffer_.c_str();
    }

    void setConfigStr(const char *config_str) override {
        glz::expected<TConfig, glz::error_ctx> config =
            glz::read_json<TConfig>(std::string(config_str));
        if (!config) {
            config_ = TConfig();
            return;
        }
        config_ = config.value();
    }

  protected:
    const TConfig &getConfig() const { return config_; }

  private:
    std::string schema_buffer_;
    std::string default_config_buffer_;
    TConfig config_;
};

template <typename T> struct ISource {
    static const InterfaceID iid;
    virtual bool waitForData(T &out, std::stop_token stoken) = 0;
    virtual void cancel() = 0;
    virtual ~ISource() = default;
};

template <typename T> struct IStage {
    static const InterfaceID iid;
    virtual void process(T &data) = 0;
    virtual ~IStage() = default;
};

template <typename T> struct ISink {
    static const InterfaceID iid;
    virtual void consume(const T &data) = 0;
    virtual ~ISink() = default;
};

REYER_REGISTER_TYPED_INTERFACE(ISource, core::EyeData, IEyeSource);
REYER_REGISTER_TYPED_INTERFACE(IStage, core::EyeData, IEyeStage);
REYER_REGISTER_TYPED_INTERFACE(ISink, core::EyeData, IEyeSink);

enum class Eye : uint8_t {
    Left = 0,
    Right,
};
struct CalibrationPoint {
    reyer::vec2<float> control_point;
    reyer::vec2<float> measured_point;
    Eye eye;
};

struct ICalibration {
    virtual void pushCalibrationPoints(const CalibrationPoint *points,
                                       size_t count) = 0;
    virtual void calibrate(core::EyeData *data) = 0;
    virtual ~ICalibration() = default;
};

class CalibrationBase : public virtual ICalibration {
  public:
    void pushCalibrationPoints(const CalibrationPoint *points,
                               size_t count) override {
        onCalibrationPointsUpdated(
            std::span<const CalibrationPoint>(points, count));
    }

    void calibrate(core::EyeData *data) override { onCalibrate(*data); }

  protected:
    virtual void onCalibrate(core::EyeData &data) = 0;

    virtual void
    onCalibrationPointsUpdated(std::span<const CalibrationPoint> points) = 0;
};

struct IPlugin : public virtual ILifecycle {
    REYER_DEFINE_INTERFACE_ID(IPlugin);
    virtual void *queryInterfaceImpl(InterfaceID id) noexcept = 0;

    template <typename I> I *queryInterface() noexcept {
        return static_cast<I *>(queryInterfaceImpl(I::iid));
    }

    virtual void setName(const char *name) = 0;
    virtual const char *getName() const = 0;

    virtual void setVersion(uint32_t version) = 0;
    virtual uint32_t getVersion() const = 0;
};

// Implemented by the host, injected into a task before init(). Deliberately
// free of HDF5 types: the host owns the file, and the HDF5 library is not
// thread-safe, so a task must never write to it directly. Calls are cheap
// (stamp + enqueue) and safe from the render thread.
struct IRecorder {
    virtual void recordEvent(int event, uint64_t timestamp) = 0;
    virtual ~IRecorder() = default;
};

struct IRender {
    REYER_DEFINE_INTERFACE_ID(IRender);
    virtual void render() = 0;
    virtual void setRenderContext(core::RenderContext ctx) = 0;
    // Valid from before init() until after shutdown(); null when a task runs
    // without a file to record into.
    virtual void setRecorder(IRecorder *recorder) = 0;
    virtual bool isFinished() const = 0;
    virtual std::unique_ptr<ICalibration> takeCalibration() = 0;
    virtual ~IRender() = default;
};

template <typename... Interfaces>
class PluginBase : public IPlugin, public virtual Interfaces... {
  public:
    void *queryInterfaceImpl(InterfaceID id) noexcept override {
        if (id == IPlugin::iid)
            return static_cast<IPlugin *>(this);

        void *out = nullptr;
        (try_one<Interfaces>(id, out) || ...);
        return out;
    }

    virtual void init() override { onInit(); }
    virtual void shutdown() override { onShutdown(); }
    virtual void reset() override { onReset(); }

    virtual void setName(const char *name) override { name_ = name; }
    virtual const char *getName() const override { return name_.c_str(); }

    virtual void setVersion(uint32_t version) override { version_ = version; }
    virtual uint32_t getVersion() const override { return version_; }

  protected:
    virtual void onInit() = 0;
    virtual void onShutdown() = 0;
    virtual void onReset() = 0;

    ~PluginBase() = default;

  private:
    template <typename I> bool try_one(InterfaceID id, void *&out) noexcept {
        if (id == I::iid) {
            out = static_cast<I *>(this);
            return true;
        }
        return false;
    }

    std::string name_;
    uint32_t version_{0};
};

template <typename T>
class SourceBase : public virtual ISource<T>,
                   public core::Thread<SourceBase<T>> {
  public:
    bool waitForData(T &out, std::stop_token stoken) override final {
        // Wait on our internal cancel token. Link the caller's token to it for
        // the duration of this wait so a pipeline-thread stop also wakes us;
        // cancel() triggers the same token directly. The stop_callback is
        // destroyed before we return, so no callback outlives this call.
        std::stop_token cancel_token = cancelToken_();
        std::stop_callback cb(stoken, [this] { cancel(); });
        return output_queue_.wait_and_pop(out, cancel_token);
    }

    void cancel() override final {
        std::lock_guard<std::mutex> lock(cancel_mutex_);
        cancel_source_.request_stop();
    }

    void startProducing() {
        // Fresh token per production run: a previous cancel()/stop must not
        // leave the source permanently stopped when it is reused.
        {
            std::lock_guard<std::mutex> lock(cancel_mutex_);
            cancel_source_ = std::stop_source{};
        }
        this->Spawn();
    }

    void stopProducing() { this->Stop(); }

    // Thread<T> CRTP interface
    void Init() {}
    void Run() {
        T sample{};
        if (onProduce(sample)) {
            // Stamp on receipt so every source shares one clock with recorded
            // events, whatever clock the underlying device uses. Sources that
            // have a device timestamp keep it in their own field.
            if constexpr (requires { sample.timestamp; }) {
                sample.timestamp = core::now_us();
            }
            output_queue_.push(std::move(sample));
        }
    }
    void Shutdown() {}

  protected:
    virtual bool onProduce(T &out) = 0;

  private:
    std::stop_token cancelToken_() {
        std::lock_guard<std::mutex> lock(cancel_mutex_);
        return cancel_source_.get_token();
    }

    core::Queue<T> output_queue_;
    std::mutex cancel_mutex_;
    std::stop_source cancel_source_;
};

template <typename T> class StageBase : public virtual IStage<T> {
  public:
    void process(T &data) override final { onProcess(data); }

  protected:
    virtual void onProcess(T &data) = 0;
};

template <typename T> class SinkBase : public virtual ISink<T> {
  public:
    void consume(const T &data) override { onConsume(data); }

  protected:
    virtual void onConsume(const T &data) = 0;
};

using EyeSourceBase = SourceBase<core::EyeData>;
using EyeStageBase = StageBase<core::EyeData>;
using EyeSinkBase = SinkBase<core::EyeData>;

class RenderBase : public virtual IRender {
  public:
    RenderBase() {};
    void render() override { onRender(); }
    void setRenderContext(core::RenderContext ctx) override final {
        render_context_ = ctx;
    }

    void setRecorder(IRecorder *recorder) override final {
        recorder_ = recorder;
    }

    std::unique_ptr<ICalibration> takeCalibration() override {
        return std::move(pending_calibration_);
    }

    bool isFinished() const override { return finished_; }
    virtual ~RenderBase() = default;

  protected:
    virtual void onRender() = 0;

    const core::RenderContext &getRenderContext() const {
        return render_context_;
    };

    // Record a timestamped marker into the task's event stream. Stamped here,
    // at the call site, so the timestamp reflects when the event happened
    // rather than when it reached the file. Safe to call from onRender() or
    // onConsume(); a no-op when the task is running without a file.
    void recordEvent(int event) {
        if (recorder_)
            recorder_->recordEvent(event, core::now_us());
    }

    template <typename E>
        requires std::is_enum_v<E>
    void recordEvent(E event) {
        recordEvent(static_cast<int>(event));
    }

    void endTask() { finished_ = true; }

    void resetFinished() {
        finished_ = false;
        pending_calibration_.reset();
    }

    void pushCalibration(std::unique_ptr<ICalibration> cal) {
        pending_calibration_ = std::move(cal);
    }

  private:
    core::RenderContext render_context_;
    IRecorder *recorder_{nullptr};
    bool finished_ = false;
    std::unique_ptr<ICalibration> pending_calibration_;
};

template <typename Config>
class RenderPluginBase
    : public PluginBase<RenderBase, ConfigurableBase<Config>, EyeSinkBase> {
  public:
    RenderPluginBase() {};

    void init() override {
        // Hold mutex_ across onInit() so it cannot race with consume()/render()
        // on the pipeline/graphics threads. initialized_ gates consume() until
        // onInit() has fully run.
        std::lock_guard<std::mutex> lock(mutex_);
        PluginBase<RenderBase, ConfigurableBase<Config>, EyeSinkBase>::init();
        initialized_ = true;
    }

    void reset() override final {
        // Serialize teardown against consume()/render(). Drop initialized_
        // first so no consume() runs against a reset-but-not-reinitialized
        // plugin.
        std::lock_guard<std::mutex> lock(mutex_);
        initialized_ = false;
        RenderBase::resetFinished();
        PluginBase<RenderBase, ConfigurableBase<Config>, EyeSinkBase>::reset();
    }

    void consume(const core::EyeData &data) override final {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!initialized_) return;
        EyeSinkBase::consume(data);
    }

    void render() override final {
        std::lock_guard<std::mutex> lock(mutex_);
        RenderBase::render();
    }

    virtual ~RenderPluginBase() = default;

  private:
    std::mutex mutex_;
    bool initialized_ = false;
};

template <typename Config>
class SourcePluginBase
    : public PluginBase<EyeSourceBase, ConfigurableBase<Config>> {
  public:
    void init() override {
        PluginBase<EyeSourceBase, ConfigurableBase<Config>>::init();
        EyeSourceBase::startProducing();
    }

    void shutdown() override {
        EyeSourceBase::stopProducing();
        PluginBase<EyeSourceBase, ConfigurableBase<Config>>::shutdown();
    }
};

} // namespace reyer::plugin
