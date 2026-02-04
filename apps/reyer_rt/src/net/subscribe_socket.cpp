#include "reyer_rt/net/subscribe_socket.hpp"
#include <nng/protocol/pubsub0/sub.h>

namespace reyer_rt::net {

SubscribeSocket::SubscribeSocket() = default;

std::error_code SubscribeSocket::Init() {
    return base_.Init(detail::SocketType::SUB);
}

std::error_code SubscribeSocket::Connect(const std::string& address) {
    return base_.Connect(address);
}

std::error_code SubscribeSocket::Subscribe(const std::string& topic) {
    // In NNG, SUB sockets subscribe to topics via socket options
    // An empty topic subscribes to all messages
    if (topic.empty()) {
        // Subscribe to all messages (empty topic filter)
        return base_.SetPointerOption(NNG_OPT_SUB_SUBSCRIBE, nullptr, 0);
    } else {
        // Subscribe to specific topic
        return base_.SetPointerOption(NNG_OPT_SUB_SUBSCRIBE, topic.data(), topic.size());
    }
}

std::error_code SubscribeSocket::Receive(std::string& data) {
    return base_.Receive(data);
}

void SubscribeSocket::Shutdown() {
    base_.Close();
}

SubscribeSocket::~SubscribeSocket() = default;

} // namespace reyer_rt::net
