#pragma once
#include <dlfcn.h>
#include <filesystem>
#include <memory>
#include <string>
#include <utility>
#include "interfaces.hpp"

namespace reyer::plugin {

// Encode major.minor.patch into a single uint32: 0xMMmmpppp
constexpr uint32_t make_version(uint8_t major, uint8_t minor, uint16_t patch) {
    return (static_cast<uint32_t>(major) << 24) |
           (static_cast<uint32_t>(minor) << 16) |
           static_cast<uint32_t>(patch);
}

struct PluginInfo {
    std::string name;
    std::string author;
    std::string description;
    uint32_t    version{0};  // encoded via make_version()
};

// Create/Destroy function types
using PluginCreateFcn      = IPlugin *(void);
using PluginDestroyFcn     = void(IPlugin *);
using PluginNameFcn        = const char *();
using PluginAuthorFcn      = const char *();
using PluginDescriptionFcn = const char *();
using PluginVersionFcn     = uint32_t();


class Plugin {
  public:
    Plugin() = default;

    Plugin(void *handle, PluginCreateFcn *create, PluginDestroyFcn *destroy,
           PluginInfo info, std::filesystem::path path = {})
        : info_(std::move(info)), path_(std::move(path)) {
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
    const std::string &getName()        const { return info_.name; }
    const std::string &getAuthor()      const { return info_.author; }
    const std::string &getDescription() const { return info_.description; }
    uint32_t           getVersion()     const { return info_.version; }
    const PluginInfo  &getInfo()        const { return info_; }
    const std::filesystem::path &getPath() const { return path_; }
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
    PluginInfo info_;
    std::filesystem::path path_;
};
} // namespace reyer::plugin

#define REYER_PLUGIN_ENTRY(CLASS_NAME, NAME, AUTHOR, DESCRIPTION, VERSION)      \
    extern "C" {                                                               \
    reyer::plugin::IPlugin *createPlugin() { return new CLASS_NAME(); }        \
    void destroyPlugin(reyer::plugin::IPlugin *plugin) { delete plugin; }      \
    const char    *pluginName()        { return NAME; }                        \
    const char    *pluginAuthor()      { return AUTHOR; }                      \
    const char    *pluginDescription() { return DESCRIPTION; }                 \
    uint32_t       pluginVersion()     { return VERSION; }                     \
    }
