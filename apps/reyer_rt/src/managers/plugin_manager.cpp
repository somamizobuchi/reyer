#include "reyer_rt/managers/plugin_manager.hpp"
#include <algorithm>
#include <dlfcn.h>
#include <filesystem>
#include <spdlog/spdlog.h>
#include <system_error>

namespace reyer_rt::managers {

PluginManager::PluginManager(const std::filesystem::path &plugins_dir) {
    LoadPluginsFromDirectory_(plugins_dir);
}

std::error_code PluginManager::LoadPlugin(const std::string &path) {
    void *handle = dlopen(path.c_str(), RTLD_LAZY | RTLD_LOCAL);

    if (!handle) {
        return std::make_error_code(std::errc::no_such_file_or_directory);
    }

    auto createPlugin = reinterpret_cast<reyer::plugin::PluginCreateFcn *>(
        dlsym(handle, "createPlugin"));
    auto destroyPlugin = reinterpret_cast<reyer::plugin::PluginDestroyFcn *>(
        dlsym(handle, "destroyPlugin"));
    auto getName = reinterpret_cast<reyer::plugin::PluginNameFcn *>(
        dlsym(handle, "pluginName"));

    if (!createPlugin || !destroyPlugin || !getName) {
        dlclose(handle);
        return std::make_error_code(std::errc::executable_format_error);
    }

    reyer::plugin::Plugin plugin(handle, createPlugin, destroyPlugin,
                                 getName());

    if (!plugin.get()) {
        return std::make_error_code(std::errc::executable_format_error);
    }

    {
        std::unique_lock lock(plugins_mutex_);
        if (plugins_.find(plugin.getName()) == plugins_.end()) {
            plugins_.emplace(plugin.getName(), std::move(plugin));
        }
    }

    return {};
}

std::expected<reyer::plugin::Plugin, std::error_code>
PluginManager::GetPlugin(const std::string &name) {
    std::shared_lock lock(plugins_mutex_);
    auto it = plugins_.find(name);
    if (it == plugins_.end()) {
        return std::unexpected(
            std::make_error_code(std::errc::no_such_device_or_address));
    }
    return it->second; // Returns copy, shares ownership via shared_ptr
}

std::vector<std::string> PluginManager::GetAvailableSources() {
    std::shared_lock lock(plugins_mutex_);
    std::vector<std::string> result;
    for (auto const &[name, plugin] : plugins_) {
        if (plugin.as<reyer::plugin::IEyeSource>()) {
            result.emplace_back(name);
        }
    }
    return result;
}

std::vector<std::string> PluginManager::GetAvailableStages() {
    std::shared_lock lock(plugins_mutex_);
    std::vector<std::string> result;
    for (auto const &[name, plugin] : plugins_) {
        if (plugin.as<reyer::plugin::IEyeStage>()) {
            result.emplace_back(name);
        }
    }
    return result;
}

std::vector<std::string> PluginManager::GetAvailableSinks() {
    std::shared_lock lock(plugins_mutex_);
    std::vector<std::string> result;
    for (auto const &[name, plugin] : plugins_) {
        if (plugin.as<reyer::plugin::IEyeSink>()) {
            result.emplace_back(name);
        }
    }
    return result;
}

std::vector<std::string> PluginManager::GetAvailableTasks() {
    std::shared_lock lock(plugins_mutex_);
    std::vector<std::string> result;
    for (auto const &[name, plugin] : plugins_) {
        if (plugin.as<reyer::plugin::IRender>()) {
            result.emplace_back(name);
        }
    }
    return result;
}

std::vector<std::string> PluginManager::GetAvailableCalibrations() {
    std::shared_lock lock(plugins_mutex_);
    std::vector<std::string> result;
    for (auto const &[name, plugin] : plugins_) {
        if (plugin.as<reyer::plugin::ICalibration>()) {
            result.emplace_back(name);
        }
    }
    return result;
}

const std::vector<std::pair<std::string, std::error_code>> &
PluginManager::GetLoadErrors() const {
    return load_errors_;
}

std::error_code PluginManager::UnloadPlugin(const std::string &name) {
    std::unique_lock lock(plugins_mutex_);
    auto it = plugins_.find(name);
    if (it == plugins_.end()) {
        return std::make_error_code(std::errc::no_such_device_or_address);
    }
    plugins_.erase(it);
    return {};
}

void PluginManager::LoadPluginsFromDirectory_(
    const std::filesystem::path &dir) {
    std::error_code ec;
    if (!std::filesystem::exists(dir, ec)) {
        spdlog::warn("Plugins directory does not exist: {}", dir.string());
        return;
    }

    if (!std::filesystem::is_directory(dir, ec)) {
        spdlog::warn("Plugins path is not a directory: {}", dir.string());
        return;
    }

    try {
        for (const auto &entry : std::filesystem::directory_iterator(dir)) {
            if (!entry.is_directory(ec))
                continue;

            const auto &subdir = entry.path();
            for (const auto &file :
                 std::filesystem::directory_iterator(subdir)) {
                if (!file.is_regular_file(ec))
                    continue;

                if (IsPluginLibrary_(file.path())) {
                    auto load_ec = LoadPlugin(file.path().string());
                    if (load_ec) {
                        load_errors_.emplace_back(file.path().string(),
                                                  load_ec);
                        spdlog::warn("Failed to load plugin {}: {}",
                                     file.path().filename().string(),
                                     load_ec.message());
                    } else {
                        spdlog::info("Loaded plugin: {}",
                                     file.path().filename().string());
                    }
                }
            }
        }
    } catch (const std::exception &e) {
        spdlog::error("Error scanning plugins directory: {}", e.what());
    }
}

bool PluginManager::IsPluginLibrary_(const std::filesystem::path &file) {
    auto extension = file.extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    return extension == ".dylib" || extension == ".so";
}

void PluginManager::InitPlugins() {
    std::shared_lock lock(plugins_mutex_);
    for (auto &[name, plugin] : plugins_) {
        plugin->init();
        spdlog::info("Initialized plugin: '{}'", name);
    }
}

void PluginManager::ShutdownPlugins() {
    std::shared_lock lock(plugins_mutex_);
    for (auto &[name, plugin] : plugins_) {
        plugin->shutdown();
        spdlog::info("Shutdown plugin: '{}'", name);
    }
}

} // namespace reyer_rt::managers
