#include "reyer_rt/managers/message_manager.hpp"
#include "reyer_rt/experiment/protocol.hpp"
#include "reyer_rt/managers/graphics_manager.hpp"
#include "reyer_rt/net/message_types.hpp"
#include "reyer_rt/utils/utils.hpp"
#include "spdlog/spdlog.h"

#include <cstring>
#include <expected>
#include <format>
#include <glaze/core/context.hpp>
#include <glaze/glaze.hpp>
#include <glaze/json/read.hpp>
#include <glaze/json/write.hpp>
#include <memory>
#include <stdexcept>
#include <string_view>
#include <system_error>
#include <vector>

namespace reyer_rt::managers {

MessageManager::MessageManager(
    std::shared_ptr<GraphicsManager> &graphics_manager,
    std::shared_ptr<PluginManager> &plugin_manager)
    : message_visitor_(*this), graphics_manager_(graphics_manager),
      plugin_manager_(plugin_manager) {};

void MessageManager::Init() {
    std::error_code ec{};

    ec = rep_.Init();
    if (ec)
        throw std::runtime_error(
            std::format("Failed to initialize socket: {}", ec.message()));

    ec = rep_.Bind("ipc:///tmp/reyer-rep.sock");
    if (ec) {
        if (ec.value() == NNG_EADDRINUSE)
            throw std::runtime_error(
                "Failed to bind to ipc address. An instance of reyer may "
                "already be running.");

        throw std::runtime_error(
            std::format("Failed to bind socket: {}", ec.value()));
    }
}

void MessageManager::Shutdown() { rep_.Shutdown(); }

std::error_code MessageManager::SendErrorResponse(const std::error_code &ec,
                                                  std::string_view payload) {
    net::message::Response response{false, ec.value(), ec.message(),
                                    std::string(payload)};

    if (auto err = glz::write_json(response, send_buffer_)) {
        response.error_message = err.custom_error_message;
    }

    return rep_.Send(send_buffer_);
}

std::error_code MessageManager::SendResponse(net::message::Response &response) {
    if (auto ec = glz::write_json(response, send_buffer_)) {
        spdlog::info("Failed to serialize message");
        return std::make_error_code(std::errc::bad_message);
    }

    if (auto ec = rep_.Send(send_buffer_)) {
        spdlog::error("Failed to send response {}", ec.message());
        return ec;
    }
    return {};
}

template<typename T>
std::expected<std::shared_ptr<T>, std::error_code>
MessageManager::LockManager(std::weak_ptr<T>& weak_ptr) {
    auto shared = weak_ptr.lock();
    if (!shared) {
        return std::unexpected(
            std::make_error_code(std::errc::resource_unavailable_try_again)
        );
    }
    return shared;
}

net::message::Response MessageManager::CreateSuccessResponse(std::string payload) {
    return net::message::Response{
        .success = true,
        .error_code = 0,
        .error_message = "",
        .payload = std::move(payload)
    };
}

net::message::Response MessageManager::CreateErrorResponse(
    std::error_code ec,
    std::string_view context
) {
    std::string error_msg = ec.message();
    if (!context.empty()) {
        error_msg = std::format("{}: {}", context, ec.message());
    }
    return net::message::Response{
        .success = false,
        .error_code = ec.value(),
        .error_message = std::move(error_msg),
        .payload = ""
    };
}

template<typename T>
std::expected<std::string, std::error_code>
MessageManager::SerializePayload(const T& data) {
    std::string buffer;
    if (auto err = glz::write_json(data, buffer)) {
        return std::unexpected(
            std::make_error_code(std::errc::bad_message)
        );
    }
    return buffer;
}

// Template instantiations for used types
template std::expected<std::shared_ptr<GraphicsManager>, std::error_code>
MessageManager::LockManager(std::weak_ptr<GraphicsManager>&);

template std::expected<std::shared_ptr<PluginManager>, std::error_code>
MessageManager::LockManager(std::weak_ptr<PluginManager>&);

template std::expected<std::string, std::error_code>
MessageManager::SerializePayload(const net::message::Pong&);

template std::expected<std::string, std::error_code>
MessageManager::SerializePayload(const std::vector<net::message::MonitorInfo>&);

template std::expected<std::string, std::error_code>
MessageManager::SerializePayload(const std::vector<net::message::PluginInfo>&);

void MessageManager::Run() {
    auto ec = rep_.Receive(recv_buffer_);
    if (ec) {
        // Timeout
        if (ec.value() == NNG_ETIMEDOUT)
            return;
        // No new message (when called with NNG_FLAG_NOBLOCK)
        if (ec.value() == NNG_EAGAIN)
            return;

        throw std::runtime_error(
            std::format("Failed to receive: {}", ec.message()));
    }

    auto expected = glz::read_json<MessageVariant>(recv_buffer_);
    if (!expected) {
        spdlog::warn("Failed to parse message: {}",
                     glz::format_error(expected.error()));
        SendErrorResponse(std::make_error_code(std::errc::bad_message), "");
        return;
    }

    // Visit message and get response
    auto result = std::visit(message_visitor_, expected.value());

    // Centralized response sending
    if (result) {
        auto response = result.value();
        SendResponse(response);
    } else {
        SendErrorResponse(result.error(), "");
    }
}

std::expected<net::message::Response, std::error_code>
MessageManager::MessageVisitor::operator()(
    const net::message::Ping &ping) {

    // Create Pong message
    net::message::Pong pong{ping.timestamp};

    // Serialize Pong
    auto payload = manager.SerializePayload(pong);
    if (!payload) {
        return std::unexpected(payload.error());
    }

    // Return success response with serialized Pong in payload
    return manager.CreateSuccessResponse(std::move(payload.value()));
}

std::expected<net::message::Response, std::error_code>
MessageManager::MessageVisitor::operator()(
    const net::message::ProtocolRequest &request) {

    // Lock graphics manager
    auto graphics_manager = manager.LockManager(manager.graphics_manager_);
    if (!graphics_manager) {
        return std::unexpected(graphics_manager.error());
    }

    // Lock plugin manager
    auto plugin_manager = manager.LockManager(manager.plugin_manager_);
    if (!plugin_manager) {
        return std::unexpected(plugin_manager.error());
    }

    // Check if plugins are available
    auto available = plugin_manager.value()->GetAvailablePlugins();
    if (available.empty()) {
        return std::unexpected(
            std::make_error_code(std::errc::no_message)
        );
    }

    auto protocol = request;
    protocol.tasks.clear();
    for (auto &task : request.tasks) {
        auto p = plugin_manager.value()->GetPlugin(task.name);
        if (!p) {
            spdlog::warn(" constructing protocol: {}",
                         p.error().message());
        }
        protocol.tasks.emplace_back(task.name, task.configuration);
    }

    // Generate UUID if not provided by client
    if (protocol.protocol_uuid.empty()) {
        protocol.protocol_uuid = utils::uuid_v4();
        spdlog::debug("Generated protocol UUID: {}", protocol.protocol_uuid);
    }

    // Set protocol on graphics manager
    if (!graphics_manager.value()->SetProtocol(protocol)) {
        return std::unexpected(
            std::make_error_code(std::errc::device_or_resource_busy)
        );
    }

    // Return success response (this was missing in the original!)
    return manager.CreateSuccessResponse();
}

std::expected<net::message::Response, std::error_code>
MessageManager::MessageVisitor::operator()(const net::message::CommandRequest &request) {
    auto graphics_manager = manager.LockManager(manager.graphics_manager_);
    if (!graphics_manager) {
        return std::unexpected(graphics_manager.error());
    }

    auto future = graphics_manager.value()->EnqueueCommand(request.command);
    auto ec = future.get();
    if (ec) {
        return std::unexpected(ec);
    }

    return manager.CreateSuccessResponse();
}


std::expected<net::message::Response, std::error_code>
MessageManager::MessageVisitor::operator()(
    const net::message::ResourceRequest &request) {

    switch (request.resource_code) {
    case net::message::ResourceCode::MONITORS: {
        // Lock graphics manager
        auto graphics_manager = manager.LockManager(manager.graphics_manager_);
        if (!graphics_manager) {
            return std::unexpected(graphics_manager.error());
        }

        // Get monitor info
        auto info = graphics_manager.value()->GetMonitorInfo();

        // Serialize monitor info
        auto payload = manager.SerializePayload(info);
        if (!payload) {
            return std::unexpected(payload.error());
        }

        // Return success response with payload
        return manager.CreateSuccessResponse(std::move(payload.value()));
    }

    case net::message::ResourceCode::PLUGINS: {
        // Lock plugin manager
        auto plugin_manager = manager.LockManager(manager.plugin_manager_);
        if (!plugin_manager) {
            return std::unexpected(plugin_manager.error());
        }

        // Build plugin info vector
        std::vector<net::message::PluginInfo> info;
        auto plugins = plugin_manager.value()->GetAvailablePlugins();
        for (auto const &plugin: plugins) {
            auto p = plugin_manager.value()->GetPlugin(plugin);
            if (p) {
                auto schema = p->get()->getConfigSchema();
                info.emplace_back(
                    std::string(p->getName()),
                    schema ? std::string(schema) : std::string("{}")
                );
            }
        }

        // Serialize plugin info
        auto payload = manager.SerializePayload(info);
        if (!payload) {
            return std::unexpected(payload.error());
        }

        // Return success response with payload
        return manager.CreateSuccessResponse(std::move(payload.value()));
    }

    default:
        // Return error for unknown resource code
        return std::unexpected(
            std::make_error_code(std::errc::invalid_argument)
        );
    }
}

} // namespace reyer_rt::managers
