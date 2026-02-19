"""Pure-Python plugin scaffolding logic — no Qt dependency."""

import re
from pathlib import Path


# ---------------------------------------------------------------------------
# Name conversion
# ---------------------------------------------------------------------------


def to_snake_case(name: str) -> str:
    s = re.sub(r"([a-z0-9])([A-Z])", r"\1_\2", name.strip())  # split CamelCase
    s = re.sub(r"[^a-zA-Z0-9]+", "_", s)  # replace any non-alnum run with _
    s = s.strip("_")
    return s.lower()


def to_pascal_case(name: str) -> str:
    return "".join(word.capitalize() for word in to_snake_case(name).split("_"))


def to_display_name(name: str) -> str:
    return " ".join(word.capitalize() for word in to_snake_case(name).split("_"))


# ---------------------------------------------------------------------------
# File templates
# ---------------------------------------------------------------------------


def _cmake(snake: str, display: str, description: str) -> str:
    desc_part = f' DESCRIPTION "{description}"' if description else ""
    return f"""\
cmake_minimum_required(VERSION 4.0)

project({snake} VERSION 1.0.0{desc_part})

find_package(reyer REQUIRED)

configure_file(
    ${{CMAKE_CURRENT_SOURCE_DIR}}/version.hpp.in
    ${{CMAKE_CURRENT_BINARY_DIR}}/version.hpp
    @ONLY
)

reyer_add_plugin({snake}
    SOURCES ${{CMAKE_CURRENT_SOURCE_DIR}}/{snake}.cpp
    RESOURCES ${{CMAKE_CURRENT_SOURCE_DIR}}/assets
    INSTALL
)

target_include_directories({snake} PRIVATE ${{CMAKE_CURRENT_BINARY_DIR}})
"""


def _version_hpp_in(author: str) -> str:
    author_define = f'#define PLUGIN_AUTHOR      "{author}"' if author else '#define PLUGIN_AUTHOR      "@PROJECT_NAME@"'
    return f"""\
#pragma once
#include <cstdint>
#include <reyer/plugin/loader.hpp>

#define PLUGIN_NAME        "@PROJECT_NAME@"
#define PLUGIN_DESCRIPTION "@PROJECT_DESCRIPTION@"
#define PLUGIN_VERSION_STR "@PROJECT_VERSION@"
{author_define}

#define PLUGIN_VERSION_MAJOR @PROJECT_VERSION_MAJOR@
#define PLUGIN_VERSION_MINOR @PROJECT_VERSION_MINOR@
#define PLUGIN_VERSION_PATCH @PROJECT_VERSION_PATCH@

inline constexpr uint32_t PLUGIN_VERSION =
    reyer::plugin::make_version(PLUGIN_VERSION_MAJOR,
                                PLUGIN_VERSION_MINOR,
                                PLUGIN_VERSION_PATCH);
"""


def _config_hpp(pascal: str, author: str, description: str) -> str:
    lines = []
    if author:
        lines.append(f"// Author: {author}")
    if description:
        lines.append(f"// {description}")
    header = "\n".join(lines) + "\n\n" if lines else ""
    return f"""\
{header}\
#pragma once
#include <glaze/core/meta.hpp>
#include <glaze/json/schema.hpp>

namespace reyer::plugin {{

struct {pascal}Config {{
    // Add configurable fields here.
    // They are serialised automatically via Glaze JSON.
    // Example:
    //   float duration_s{{5.0f}};
}};

}} // namespace reyer::plugin
"""


def _hpp(pascal: str, snake: str) -> str:
    return f"""\
#pragma once
#include "reyer/plugin/interfaces.hpp"
#include "{snake}_config.hpp"

namespace reyer::plugin {{

class {pascal} : public RenderPluginBase<{pascal}Config> {{
  public:
    {pascal}() = default;
    ~{pascal}() override = default;

  protected:
    void onInit() override;
    void onShutdown() override;
    void onPause() override;
    void onResume() override;
    void onReset() override;

    void onRender() override;
    void onConsume(const core::EyeData &data) override;
}};

}} // namespace reyer::plugin
"""


def _cpp(pascal: str, snake: str, display: str) -> str:
    return f"""\
#include "{snake}.hpp"
#include "reyer/plugin/loader.hpp"
#include "version.hpp"
#include <raylib.h>

namespace reyer::plugin {{

void {pascal}::onInit() {{
}}

void {pascal}::onShutdown() {{
}}

void {pascal}::onPause() {{
}}

void {pascal}::onResume() {{
}}

void {pascal}::onReset() {{
}}

void {pascal}::onRender() {{
    // Access config via getConfig().
    // Call endTask() when the stimulus is complete.
    DrawText("{display}", 10, 10, 24, WHITE);
}}

void {pascal}::onConsume(const core::EyeData &data) {{
    // Called on each new eye-tracking sample (under the render mutex).
}}

}} // namespace reyer::plugin

REYER_PLUGIN_ENTRY(reyer::plugin::{pascal}, "{display}", PLUGIN_AUTHOR, PLUGIN_DESCRIPTION, PLUGIN_VERSION)
"""


# ---------------------------------------------------------------------------
# Generator
# ---------------------------------------------------------------------------


def generate_plugin(
    name: str,
    class_name: str,
    author: str,
    description: str,
    output_dir: Path,
) -> Path:
    """Generate a plugin scaffold and return the created directory.

    Raises FileExistsError if the target directory already exists.
    """
    snake = to_snake_case(name)
    pascal = class_name.strip() if class_name.strip() else to_pascal_case(name)
    display = to_display_name(name)

    plugin_dir = output_dir / snake
    if plugin_dir.exists():
        raise FileExistsError(f"'{plugin_dir}' already exists.")

    assets_dir = plugin_dir / "assets"
    assets_dir.mkdir(parents=True)
    (assets_dir / ".gitkeep").touch()

    files = {
        plugin_dir / "CMakeLists.txt":        _cmake(snake, display, description),
        plugin_dir / "version.hpp.in":        _version_hpp_in(author),
        plugin_dir / f"{snake}_config.hpp":   _config_hpp(pascal, author, description),
        plugin_dir / f"{snake}.hpp":          _hpp(pascal, snake),
        plugin_dir / f"{snake}.cpp":          _cpp(pascal, snake, display),
    }
    for path, content in files.items():
        path.write_text(content)

    return plugin_dir
