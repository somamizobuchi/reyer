#pragma once
#include "reyer/core/core.hpp"
#include "reyer/core/queue.hpp"
#include <atomic>
#include <chrono>
#include <glaze/core/context.hpp>
#include <glaze/core/reflect.hpp>
#include <glaze/json/lazy.hpp>
#include <glaze/json/schema.hpp>
#include <glaze/util/expected.hpp>
#include <mutex>
#include <span>
#include <stdexcept>
#include <string>

namespace reyer::plugin {

enum class PluginType : uint32_t {
    RENDER = 0x0,
    TASK = 0x1,
    CALIBRATION = 0x2,
};

// Forward declarations
class IRender;

class ILifecycle {
  public:
    virtual void init() = 0;
    virtual void pause() = 0;
    virtual void resume() = 0;
    virtual void shutdown() = 0;
    virtual void reset() = 0;
    virtual ~ILifecycle() = default;
};

class IConfigurable {
  public:
    virtual const char *getConfigSchema() = 0;
    virtual void setConfigStr(const char *config_str) = 0;
    virtual ~IConfigurable() = 0;
};

template <typename T> class IConsumer {
  public:
    virtual void push(T *Data, size_t size);
    virtual ~IConsumer() = default;
};

template <typename T> class IProducer {
  public:
    virtual bool pop(T *Data);
    virtual ~IProducer() = default;
};

class IPlugin : public ILifecycle, public IConfigurable {
  public:
    virtual PluginType getType() const = 0;

    // Type-safe accessors for specific plugin interfaces
    virtual IRender* asRender() { return nullptr; }

    virtual ~IPlugin() = default;
};

class IRender : public IProducer<core::UserEvent>,
                public IConsumer<core::Data> {
  public:
    virtual void render() = 0;
    virtual bool isFinished() = 0;
    virtual ~IRender() = default;
};

template <typename TConfig> class PluginBase : public IPlugin {
  public:
    virtual void init() override { onInit(); }
    virtual void pause() override { onPause(); }
    virtual void resume() override { onResume(); }
    virtual void shutdown() override { onShutdown(); }
    virtual void reset() override { onReset(); }

    virtual const char *getConfigSchema() override final {
        auto err = glz::write_json_schema<TConfig>(schema_buffer_);
        if (err) {
            return nullptr;
        }
        return schema_buffer_.c_str();
    }

    virtual void setConfigStr(const char *config_str) override final {
        glz::expected<TConfig, glz::error_ctx> config =
            glz::read_json<TConfig>(std::string(config_str));
        if (!config) {
            configuration_ = TConfig();
            return;
        }
        configuration_ = config.value();
    };

  protected:

    virtual void onInit() = 0;
    virtual void onPause() = 0;
    virtual void onResume() = 0;
    virtual void onShutdown() = 0;
    virtual void onReset() = 0;

    const TConfig &getConfiguration() const { return configuration_; }

  private:
    std::string schema_buffer_;
    TConfig configuration_;
};

template <typename TConfig>
class RenderPluginBase : public PluginBase<TConfig>, public IRender {
  public:
    virtual ~RenderPluginBase() = default;

    virtual PluginType getType() const override final {
        return PluginType::RENDER;
    }

    virtual IRender* asRender() override { return this; }

    virtual void render() override final { 
        std::lock_guard<std::mutex> lock(data_mutex_);
        onRender(); 
    }

    virtual void push(core::Data *data, size_t size) override final {
        if (!data) {
            return;
        }
        std::span<const core::Data> user_data(data, size);
        std::lock_guard<std::mutex> lock(data_mutex_);
        onData(user_data);
    }

    virtual bool pop(core::UserEvent *event) override final {
        auto data = output_queue_.try_pop();
        if (!data) {
            return false;
        }
        *event = data.value();
        return true;
    }

    bool isFinished() override {
        return finished_.load(std::memory_order_acquire);
    }

    virtual void reset() override {
        finished_.store(false, std::memory_order_release) ;
        PluginBase<TConfig>::reset();
    }

  protected:
    virtual void onRender() = 0;
    virtual void onData(std::span<const core::Data> data) = 0;

    void recordEvent(int event) {
        uint64_t timestamp =
            std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::steady_clock::now().time_since_epoch())
                .count();
        output_queue_.push({timestamp, event});
    }

    void endTask() {
        finished_.store(true, std::memory_order_release);
    }


  private:
    core::Queue<core::UserEvent> output_queue_;
    std::atomic<bool> finished_{false};
    std::mutex data_mutex_;
};

} // namespace reyer::plugin
