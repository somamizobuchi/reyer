#pragma once
#include <dlfcn.h>
#include <memory>
#include <string>
#include <utility>
#include "interfaces.hpp"

namespace reyer::plugin {
// Create/Destroy function types
using PluginCreateFcn = IPlugin *(void);
using PluginDestroyFcn = void(IPlugin *);
using PluginNameFcn = const char *();


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

    // Type-safe casting using COM-style queryInterface
    template <typename T>
    T* as() const {
        if (!instance_) return nullptr;
        return instance_->queryInterface<T>();
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
