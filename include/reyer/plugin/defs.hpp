#pragma once
#include "iplugin.hpp"
#include <dlfcn.h>
#include <memory>
#include <string>
#include <type_traits>
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

        // Cache typed pointers at construction (one-time cost)
        if (instance_) {
            switch (instance_->getType()) {
                case PluginType::RENDER:
                    render_ = dynamic_cast<IRender*>(instance_.get());
                    break;
                case PluginType::SOURCE:
                    // source_ = dynamic_cast<ISource*>(instance_.get());
                    break;
                case PluginType::CALIBRATION:
                    // calibration_ = dynamic_cast<ICalibration*>(instance_.get());
                    break;
            }
        }
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

    // Zero-cost type-safe casting using pre-cached pointers
    // Pointers are computed once at construction via dynamic_cast
    template <typename T>
    T* as() const {
        // Delegate to specialized getters (compile-time dispatch)
        if constexpr (std::is_same_v<T, IRender>) {
            return render_;
        }
        // Add more specializations as needed:
        // else if constexpr (std::is_same_v<T, ISource>) {
        //     return source_;
        // }
        else {
            return nullptr;
        }
    }

  private:
    std::shared_ptr<void> handle_;
    std::shared_ptr<IPlugin> instance_;
    std::string name_;

    // Cached interface pointers (computed once at construction)
    IRender* render_ = nullptr;
    // Add more as plugin types are added:
    // ISource* source_ = nullptr;
    // ICalibration* calibration_ = nullptr;
};
} // namespace reyer::plugin

#define REYER_PLUGIN_ENTRY(CLASS_NAME, NAME, VERSION)                          \
    extern "C" {                                                               \
    reyer::plugin::IPlugin *createPlugin() { return new CLASS_NAME(); }        \
    void destroyPlugin(reyer::plugin::IPlugin *plugin) { delete plugin; }      \
    const char *pluginName() { return NAME; }                                  \
    }
