#include "reyer_rt/managers/broadcast_manager.hpp"
#include "reyer_rt/net/message_types.hpp"
#include <cstdint>
#include <glaze/glaze.hpp>
#include <spdlog/spdlog.h>

#include <format>
#include <stdexcept>
#include <system_error>

namespace reyer_rt::managers {

BroadcastManager::BroadcastManager() {}

void BroadcastManager::Init() {
    std::error_code ec{};

    ec = pub_.Init();
    if (ec)
        throw std::runtime_error(std::format(
            "Failed to initialize broadcast socket: {}", ec.message()));

    pub_.RegisterConnectCallback([this](uint32_t id) {});

    pub_.RegisterDisconnectCallback([this](uint32_t id) {});

    ec = pub_.Bind("ipc:///tmp/reyer-pub.sock");
    if (ec) {
        throw std::runtime_error(
            std::format("Failed to bind broadcast socket: {}", ec.message()));
    }

    spdlog::info("BroadcastManager initialized on ipc:///tmp/reyer-pub.sock");
}

void BroadcastManager::Shutdown() {
    pub_.Shutdown();
    spdlog::info("BroadcastManager shut down");
}

void BroadcastManager::Run() {
    net::message::BroadcastMessage msg;
    if (!message_queue_.wait_and_pop(msg, get_stop_token())) {
        return; // Thread stopped
    }
    if (auto ec = glz::write_json(msg, send_buffer_)) {
        spdlog::warn("Failed to serialize broadcast message: {}",
                     glz::format_error(ec));
        return;
    }
    // Actually publish the message!
    if (auto ec = pub_.Publish(send_buffer_)) {
        spdlog::warn("Failed to publish broadcast message: {}", ec.message());
    }
}

void BroadcastManager::Broadcast(
    const net::message::BroadcastMessage &message) {
    message_queue_.push(message);
}

} // namespace reyer_rt::managers
