#include "reyer_rt/net/detail/socket_base.hpp"
#include "nng/nng.h"
#include <nng/protocol/reqrep0/rep.h>
#include <nng/protocol/reqrep0/req.h>
#include <nng/protocol/pubsub0/pub.h>
#include <nng/protocol/pubsub0/sub.h>
#include <print>

namespace reyer_rt::net::detail {

SocketBase::SocketBase() = default;

SocketBase::~SocketBase() {
    Close();
}

std::error_code SocketBase::Init(SocketType type) {
    int ret = 0;

    switch (type) {
        case SocketType::REP:
            ret = nng_rep0_open(&socket_);
            break;
        case SocketType::REQ:
            ret = nng_req0_open(&socket_);
            break;
        case SocketType::PUB:
            ret = nng_pub0_open(&socket_);
            break;
        case SocketType::SUB:
            ret = nng_sub0_open(&socket_);
            break;
    }

    if (ret != 0) {
        std::println("Failed to open socket: {}", nng_strerror(ret));
        return make_error_code(ret);
    }

    is_open_ = true;
    return {};
}

std::error_code SocketBase::Bind(const std::string& address) {
    if (!is_open_) {
        return make_error_code(NNG_ECLOSED);
    }

    int ret = nng_listen(socket_, address.c_str(), &listener_, 0);
    if (ret != 0) {
        return make_error_code(ret);
    }

    return {};
}

std::error_code SocketBase::Connect(const std::string& address) {
    if (!is_open_) {
        return make_error_code(NNG_ECLOSED);
    }

    int ret = nng_dial(socket_, address.c_str(), &dialer_, 0);
    if (ret != 0) {
        return make_error_code(ret);
    }

    return {};
}

std::error_code SocketBase::Send(const std::string& data) {
    if (!is_open_) {
        return make_error_code(NNG_ECLOSED);
    }

    nng_msg* msg = nullptr;
    int ret = nng_msg_alloc(&msg, 0);
    if (ret != 0) {
        return make_error_code(ret);
    }

    MessagePtr send_msg(msg);

    ret = nng_msg_append(send_msg.get(), static_cast<const void*>(data.data()), data.size());
    if (ret != 0) {
        return make_error_code(ret);
    }

    // nng_sendmsg takes ownership of the message, so we release it
    ret = nng_sendmsg(socket_, send_msg.release(), NNG_FLAG_NONBLOCK);
    if (ret != 0) {
        return make_error_code(ret);
    }

    return {};
}

std::error_code SocketBase::Receive(std::string& data) {
    if (!is_open_) {
        return make_error_code(NNG_ECLOSED);
    }

    nng_msg* msg = nullptr;
    int ret = nng_recvmsg(socket_, &msg, 0);
    if (ret != 0) {
        return make_error_code(ret);
    }

    MessagePtr recv_msg(msg);

    auto len = nng_msg_len(recv_msg.get());
    auto beg = reinterpret_cast<char*>(nng_msg_body(recv_msg.get()));
    data.assign(beg, beg + len);

    return {};
}

void SocketBase::Close() {
    if (listener_.id != 0) {
        nng_listener_close(listener_);
        listener_ = NNG_LISTENER_INITIALIZER;
    }

    if (dialer_.id != 0) {
        nng_dialer_close(dialer_);
        dialer_ = NNG_DIALER_INITIALIZER;
    }

    if (is_open_) {
        nng_socket_close(socket_);
        socket_ = NNG_SOCKET_INITIALIZER;
        is_open_ = false;
    }
}

bool SocketBase::IsOpen() const {
    return is_open_;
}

std::error_code SocketBase::SetIntOption(const char* name, int value) {
    if (!is_open_) {
        return make_error_code(NNG_ECLOSED);
    }

    int ret = nng_socket_set_int(socket_, name, value);
    if (ret != 0) {
        return make_error_code(ret);
    }

    return {};
}

std::error_code SocketBase::SetPointerOption(const char* name, const void* value, size_t len) {
    if (!is_open_) {
        return make_error_code(NNG_ECLOSED);
    }

    int ret = nng_socket_set(socket_, name, value, len);
    if (ret != 0) {
        return make_error_code(ret);
    }

    return {};
}

} // namespace reyer_rt::net::detail
