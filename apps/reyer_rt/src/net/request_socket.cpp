#include "reyer_rt/net/request_socket.hpp"

namespace reyer_rt::net {

RequestSocket::RequestSocket() = default;

std::error_code RequestSocket::Init() {
    return base_.Init(detail::SocketType::REQ);
}

std::error_code RequestSocket::Connect(const std::string& address) {
    return base_.Connect(address);
}

std::error_code RequestSocket::Request(const std::string& request,
                                       std::string& reply) {
    auto ec = base_.Send(request);
    if (ec) {
        return ec;
    }

    return base_.Receive(reply);
}

void RequestSocket::Shutdown() {
    base_.Close();
}

RequestSocket::~RequestSocket() = default;

} // namespace reyer_rt::net
