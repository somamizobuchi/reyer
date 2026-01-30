#pragma once

#include "detail/socket_base.hpp"
#include <system_error>

namespace reyer_rt::net {

// Client-side socket for SUB (subscribe) pattern
// Subscribes to and receives messages from publishers
class SubscribeSocket {
public:
    SubscribeSocket();

    // Initialize SUB socket
    std::error_code Init();

    // Connect to publisher address
    std::error_code Connect(const std::string& address);

    // Subscribe to all messages (empty topic = receive all)
    std::error_code Subscribe(const std::string& topic = {});

    // Receive a published message
    std::error_code Receive(std::string& data);

    // Shutdown the socket
    void Shutdown();

    ~SubscribeSocket();

private:
    detail::SocketBase base_;
};

} // namespace reyer_rt::net
