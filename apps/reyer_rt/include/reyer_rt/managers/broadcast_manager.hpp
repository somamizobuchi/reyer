#pragma once

#include "reyer/core/queue.hpp"
#include "reyer_rt/net/publish_socket.hpp"
#include "reyer_rt/threading/thread.hpp"
#include "reyer_rt/net/message_types.hpp"
#include "spdlog/spdlog.h"
#include <glaze/json/write.hpp>
#include <string>
#include <system_error>
#include <variant>

namespace reyer_rt::managers {

class BroadcastManager : public threading::Thread<BroadcastManager> {
  public:
    using MessageVariant = std::variant<net::message::Ping>;

    BroadcastManager();

    void Init();
    void Shutdown();
    void Run();

    // Broadcast a message to all subscribers
    void Broadcast(const net::message::BroadcastMessage &message);

    // Serialize payload
    template<typename T>
    std::error_code Broadcast(const net::message::BroadcastTopic &topic, const T& payload) {
        net::message::BroadcastMessage message;
        if (auto ec = glz::write_json(payload, payload_buffer_)) {
            spdlog::warn("Failed to serialize broadcast message: {}", glz::format_error(ec));
            return std::make_error_code(std::errc::bad_message);
        }

        message.topic = topic;
        message.payload = payload_buffer_;

        Broadcast(message);

        return {};
    }

    ~BroadcastManager() = default;

  private:
    net::PublishSocket pub_;
    std::string send_buffer_;
    std::string payload_buffer_;
    reyer::core::Queue<net::message::BroadcastMessage> message_queue_;
    MessageVariant message_;
};

} // namespace reyer_rt::managers
