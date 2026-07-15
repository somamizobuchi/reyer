#pragma once

#include "reyer/plugin/loader.hpp"
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
    explicit PluginManager(std::vector<std::filesystem::path> plugin_dirs);
    ~PluginManager() = default;

    std::error_code LoadPlugin(const std::string &path);

    // Returns a shared template instance for cheap metadata/schema queries.
    // Do NOT run a task/pipeline through this instance — it is shared across
    // all callers. Use CreateInstance() for anything that runs a lifecycle.
    std::expected<reyer::plugin::Plugin, std::error_code>
    GetPlugin(const std::string &name);

    // Returns a Plugin owning a freshly-constructed instance of the named
    // plugin, independent of the shared template and of other instances. Use
    // this for every execution run (protocol tasks, pipeline source/stages).
    std::expected<reyer::plugin::Plugin, std::error_code>
    CreateInstance(const std::string &name);

    std::vector<std::string> GetAvailableSources();
    std::vector<std::string> GetAvailableStages();
    std::vector<std::string> GetAvailableSinks();
    std::vector<std::string> GetAvailableTasks();

    std::error_code UnloadPlugin(const std::string &name);

    void InitPlugins();
    void ShutdownPlugins();

  private:
    std::unordered_map<std::string, reyer::plugin::Plugin> plugins_;
    mutable std::shared_mutex plugins_mutex_;

    void LoadPluginsFromDirectory_(const std::filesystem::path &dir);
    static bool IsPluginLibrary_(const std::filesystem::path &file);
};

} // namespace reyer_rt::managers
