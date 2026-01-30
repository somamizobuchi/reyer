#pragma once

#include "reyer/plugin/defs.hpp"
#include <expected>
#include <reyer/plugin/plugin.hpp>

#include <filesystem>
#include <shared_mutex>
#include <string>
#include <system_error>
#include <unordered_map>
#include <vector>

namespace reyer_rt::managers {

class PluginManager {
  public:
    explicit PluginManager(const std::filesystem::path &plugins_dir);

    std::error_code LoadPlugin(const std::string &path);

    std::expected<reyer::plugin::Plugin, std::error_code>
    GetPlugin(const std::string &name);

    std::vector<std::string> GetAvailablePlugins();

    const std::vector<std::pair<std::string, std::error_code>> &
    GetLoadErrors() const;

    std::error_code UnloadPlugin(const std::string &name);

  private:
    std::unordered_map<std::string, reyer::plugin::Plugin> plugins_;
    mutable std::shared_mutex plugins_mutex_;
    std::vector<std::pair<std::string, std::error_code>> load_errors_;

    void LoadPluginsFromDirectory_(const std::filesystem::path &dir);
    static bool IsPluginLibrary_(const std::filesystem::path &file);
};

} // namespace reyer_rt::managers
