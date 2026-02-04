#include "reyer_rt/net/publish_socket.hpp"

namespace reyer_rt::net {

PublishSocket::PublishSocket() = default;

std::error_code PublishSocket::Init() {
    return base_.Init(detail::SocketType::PUB);
}

std::error_code PublishSocket::Bind(const std::string& address) {
    return base_.Bind(address);
}

std::error_code PublishSocket::Publish(const std::string& data) {
    return base_.Send(data);
}

void PublishSocket::RegisterConnectCallback(detail::SocketBase::pipe_cb_t callback) {
    base_.RegisterConnectCallback(std::move(callback));
};

void PublishSocket::RegisterDisconnectCallback(detail::SocketBase::pipe_cb_t callback) {
    base_.RegisterDisconnectCallback(std::move(callback));
};

void PublishSocket::Shutdown() {
    base_.Close();
}

PublishSocket::~PublishSocket() = default;

} // namespace reyer_rt::net
