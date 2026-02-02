#pragma once

#include "detail/socket_base.hpp"
#include <cstddef>
#include <system_error>

namespace reyer_rt::net {

// Server-side socket for REP (reply) pattern
// Receives requests and sends replies
class ReplySocket {
public:
    ReplySocket();

    // Initialize REP socket
    std::error_code Init();

    // Bind to address and listen for connections
    std::error_code Bind(const std::string& address);

    // Receive a request
    std::error_code Receive(std::string& data);

    // Send a reply to the request
    std::error_code Send(const std::string& data);

    // Register callback for connection
    void RegisterConnectCallback(detail::SocketBase::pipe_cb_t callback);

    // Register callback for pipe disconnect
    void RegisterDisconnectCallback(detail::SocketBase::pipe_cb_t callback);

    // Shutdown the socket
    void Shutdown();

    ~ReplySocket();

private:
    detail::SocketBase base_;
};

} // namespace reyer_rt::net
