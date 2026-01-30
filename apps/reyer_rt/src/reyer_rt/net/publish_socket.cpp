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

void PublishSocket::Shutdown() {
    base_.Close();
}

PublishSocket::~PublishSocket() = default;

} // namespace reyer_rt::net
