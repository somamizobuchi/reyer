#pragma once
#include "reyer/core/core.hpp"
#include "reyer/core/utils.hpp"
#include <glaze/core/context.hpp>
#include <glaze/json/read.hpp>
#include <glaze/json/schema.hpp>
#include <glaze/util/expected.hpp>
#include <span>
#include <string>

#define REYER_DEFINE_INTERFACE_ID(name)                                        \
    static constexpr InterfaceID iid = {reyer::core::hash_string(#name)};

#define REYER_REGISTER_TYPED_INTERFACE(Interface, Type, Alias)                 \
    template <>                                                                \
    inline constexpr InterfaceID Interface<Type>::iid = {                      \
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
    virtual void pause() = 0;
    virtual void resume() = 0;
    virtual void shutdown() = 0;
    virtual void reset() = 0;
    virtual ~ILifecycle() = default;
};

struct IConfigurable {
    REYER_DEFINE_INTERFACE_ID(IConfigurable)
    virtual const char *getConfigSchema() = 0;
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
    TConfig config_;
};

template <typename T> struct IStage {
    static constexpr InterfaceID iid{};
    virtual void process(T *data, size_t length) = 0;
    virtual ~IStage() = default;
};

REYER_REGISTER_TYPED_INTERFACE(IStage, core::EyeData, IEyeStage);

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

struct IRender {
    REYER_DEFINE_INTERFACE_ID(IRender);
    virtual void render() = 0;
    virtual void setRenderContext(core::RenderContext ctx) = 0;
    virtual bool isFinished() const = 0;
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
    virtual void pause() override { onPause(); }
    virtual void resume() override { onResume(); }
    virtual void reset() override { onReset(); }

    virtual void setName(const char *name) override { name_ = name; }
    virtual const char *getName() const override { return name_.c_str(); }

    virtual void setVersion(uint32_t version) override { version_ = version; }
    virtual uint32_t getVersion() const override { return version_; }

  protected:
    virtual void onInit() = 0;
    virtual void onShutdown() = 0;
    virtual void onPause() = 0;
    virtual void onResume() = 0;
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

class EyeStageBase : public virtual IEyeStage {
  public:
    virtual void process(core::EyeData *data, size_t length) override final {
        if (data)
            onProcess(std::span<core::EyeData>(data, length));
    }

  protected:
    virtual void onProcess(std::span<core::EyeData> data) = 0;
};

class RenderBase : public virtual IRender {
  public:
    RenderBase() {};
    void render() override final { onRender(); }
    void setRenderContext(core::RenderContext ctx) override final {
        render_context_ = ctx;
    }
    bool isFinished() const override { return finished_; }
    virtual ~RenderBase() = default;

  protected:
    virtual void onRender() = 0;
    const core::RenderContext &getRenderContext() const {
        return render_context_;
    };
    void endTask() { finished_ = true; }

  private:
    core::RenderContext render_context_;
    bool finished_ = false;
};

template <typename Config>
using RenderPluginBase =
    PluginBase<RenderBase, ConfigurableBase<Config>, EyeStageBase>;
} // namespace reyer::plugin
