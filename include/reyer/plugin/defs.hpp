#pragma once
#include "iplugin.hpp"
#include <dlfcn.h>
#include <memory>
#include <string>
#include <utility>

namespace reyer::plugin {
// Create/Destroy function types
using PluginCreateFcn = IPlugin *(void);
using PluginDestroyFcn = void(IPlugin *);
using PluginNameFcn = const char *();

// Helper: Map interface types to PluginType enum values
template <typename T>
struct PluginTypeTraits;

template <>
struct PluginTypeTraits<IRender> {
    static constexpr PluginType value = PluginType::RENDER;
};

// Add more specializations as new plugin types are added:
// template <>
// struct PluginTypeTraits<ISource> {
//     static constexpr PluginType value = PluginType::SOURCE;
// };

class Plugin {
  public:
    Plugin() = default;

    Plugin(void *handle, PluginCreateFcn *create, PluginDestroyFcn *destroy,
           std::string name)
        : name_(std::move(name)) {
        // Shared handle with custom deleter
        handle_ = std::shared_ptr<void>(handle, [](void* h) {
            if (h) dlclose(h);
        });

        // Shared plugin instance with custom deleter
        instance_ = std::shared_ptr<IPlugin>(create(), [destroy](IPlugin* p) {
            if (p && destroy) destroy(p);
        });
    }

    // Copyable - shares ownership via shared_ptr
    Plugin(const Plugin&) = default;
    Plugin& operator=(const Plugin&) = default;
    Plugin(Plugin&&) = default;
    Plugin& operator=(Plugin&&) = default;

    IPlugin *get() const { return instance_.get(); }
    IPlugin *operator->() const { return get(); }
    const std::string &getName() const { return name_; }
    explicit operator bool() const { return instance_ != nullptr; }

    // Type-safe casting using PluginType enum (no dynamic_cast)
    // Safe because PluginType check guarantees the concrete type implements T
    template <typename T>
    T* as() const {
        if (!instance_) return nullptr;

        // Check if the runtime type matches the requested type
        if (instance_->getType() == PluginTypeTraits<T>::value) {
            // reinterpret_cast is safe here: we've verified via PluginType
            // that the concrete plugin class implements both IPlugin and T
            return reinterpret_cast<T*>(instance_.get());
        }

        return nullptr;
    }

  private:
    std::shared_ptr<void> handle_;
    std::shared_ptr<IPlugin> instance_;
    std::string name_;
};
} // namespace reyer::plugin

#define REYER_PLUGIN_ENTRY(CLASS_NAME, NAME, VERSION)                          \
    extern "C" {                                                               \
    reyer::plugin::IPlugin *createPlugin() { return new CLASS_NAME(); }        \
    void destroyPlugin(reyer::plugin::IPlugin *plugin) { delete plugin; }      \
    const char *pluginName() { return NAME; }                                  \
    }
