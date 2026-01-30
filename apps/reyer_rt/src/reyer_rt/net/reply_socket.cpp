#include "reyer_rt/net/reply_socket.hpp"
#include <cstddef>
#include <string>
#include <system_error>

namespace reyer_rt::net {

ReplySocket::ReplySocket() = default;

std::error_code ReplySocket::Init() {
    auto ec = base_.Init(detail::SocketType::REP);
    if (ec) return ec;

    int timeout = 100;
    return base_.SetPointerOption(NNG_OPT_RECVTIMEO, &timeout, sizeof(int));
}

std::error_code ReplySocket::Bind(const std::string& address) {
    return base_.Bind(address);
}

std::error_code ReplySocket::Receive(std::string& data) {
    return base_.Receive(data);
}

std::error_code ReplySocket::Send(const std::string& data) {
    return base_.Send(data);
}

void ReplySocket::Shutdown() {
    base_.Close();
}

ReplySocket::~ReplySocket() = default;

} // namespace reyer_rt::net
