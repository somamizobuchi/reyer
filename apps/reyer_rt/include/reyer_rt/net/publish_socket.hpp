#pragma once

#include "detail/socket_base.hpp"
#include <system_error>

namespace reyer_rt::net {

// Server-side socket for PUB (publish) pattern
// Publishes messages to subscribers
class PublishSocket {
public:
    PublishSocket();

    // Initialize PUB socket
    std::error_code Init();

    // Bind to address and listen for subscribers
    std::error_code Bind(const std::string& address);

    // Publish a message
    std::error_code Publish(const std::string& data);

    // Shutdown the socket
    void Shutdown();

    ~PublishSocket();

private:
    detail::SocketBase base_;
};

} // namespace reyer_rt::net
