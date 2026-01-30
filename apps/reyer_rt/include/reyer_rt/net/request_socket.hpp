#pragma once

#include "detail/socket_base.hpp"
#include <system_error>

namespace reyer_rt::net {

// Client-side socket for REQ (request) pattern
// Sends requests and receives replies
class RequestSocket {
public:
    RequestSocket();

    // Initialize REQ socket
    std::error_code Init();

    // Connect to server address
    std::error_code Connect(const std::string& address);

    // Send a request and receive a reply
    std::error_code Request(const std::string& request,
                           std::string& reply);

    // Shutdown the socket
    void Shutdown();

    ~RequestSocket();

private:
    detail::SocketBase base_;
};

} // namespace reyer_rt::net
