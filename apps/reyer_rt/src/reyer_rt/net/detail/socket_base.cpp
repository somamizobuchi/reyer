#include "reyer_rt/net/detail/socket_base.hpp"
#include "nng/nng.h"
#include <nng/protocol/pubsub0/pub.h>
#include <nng/protocol/pubsub0/sub.h>
#include <nng/protocol/reqrep0/rep.h>
#include <nng/protocol/reqrep0/req.h>
#include <spdlog/spdlog.h>
#include <print>

namespace reyer_rt::net::detail {

SocketBase::SocketBase() = default;

SocketBase::~SocketBase() { Close(); }

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

    // Register callbacks for connect and disconnect
    nng_pipe_notify(socket_, NNG_PIPE_EV_ADD_POST, &SocketBase::handlePipeNotify_, this);
    nng_pipe_notify(socket_, NNG_PIPE_EV_REM_POST, &SocketBase::handlePipeNotify_, this);

    if (ret != 0) {
        std::println("Failed to open socket: {}", nng_strerror(ret));
        return make_error_code(ret);
    }

    is_open_ = true;
    return {};
}

std::error_code SocketBase::Bind(const std::string &address) {
    if (!is_open_) {
        return make_error_code(NNG_ECLOSED);
    }

    int ret = nng_listen(socket_, address.c_str(), &listener_, 0);
    if (ret != 0) {
        return make_error_code(ret);
    }

    return {};
}

std::error_code SocketBase::Connect(const std::string &address) {
    if (!is_open_) {
        return make_error_code(NNG_ECLOSED);
    }

    int ret = nng_dial(socket_, address.c_str(), &dialer_, 0);
    if (ret != 0) {
        return make_error_code(ret);
    }

    return {};
}

std::error_code SocketBase::Send(const std::string &data) {
    if (!is_open_) {
        return make_error_code(NNG_ECLOSED);
    }
    int ret = 0;
    
    nng_msg* msg;

    ret = nng_msg_alloc(&msg, 0);
    if (ret != 0) {
        return make_error_code(ret);
    }

    ret = nng_msg_append(msg, static_cast<const void *>(data.data()),
                         data.size());
    if (ret != 0) {
        return make_error_code(ret);
    }

    // nng_sendmsg takes ownership of the message, so we release it
    ret = nng_sendmsg(socket_, msg, NNG_FLAG_NONBLOCK);
    if (ret != 0) {
        return make_error_code(ret);
    }

    return {};
}

std::error_code SocketBase::Receive(std::string &data) {
    if (!is_open_) {
        return make_error_code(NNG_ECLOSED);
    }

    int ret = 0;
    if (!msg_) {
        ret = nng_msg_alloc(&msg_, 0);
        if (ret != 0) {
            return make_error_code(ret);
        }
    }

    ret = nng_recvmsg(socket_, &msg_, 0);
    if (ret != 0) {
        return make_error_code(ret);
    }

    auto len = nng_msg_len(msg_);
    auto beg = reinterpret_cast<char *>(nng_msg_body(msg_));
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

    if (msg_) {
        nng_msg_free(msg_);
    }
}

bool SocketBase::IsOpen() const { return is_open_; }

void SocketBase::RegisterConnectCallback(SocketBase::pipe_cb_t callback) {
    connect_cb_ = std::move(callback);
}

void SocketBase::RegisterDisconnectCallback(SocketBase::pipe_cb_t callback) {
    disconnect_cb_ = std::move(callback);
}

void SocketBase::handlePipeNotify_(nng_pipe pipe, nng_pipe_ev event, void *user_data) {
    auto self = static_cast<SocketBase *>(user_data);

    switch (event) {
        case NNG_PIPE_EV_ADD_POST:
            if (self->connect_cb_) {
                self->connect_cb_(pipe.id);
            }
            break;
        case NNG_PIPE_EV_REM_POST:
            if (self->disconnect_cb_) {
                self->disconnect_cb_(pipe.id);
            }
            break;
        default:
            break;
    }
}

std::error_code SocketBase::SetIntOption(const char *name, int value) {
    if (!is_open_) {
        return make_error_code(NNG_ECLOSED);
    }

    int ret = nng_socket_set_int(socket_, name, value);
    if (ret != 0) {
        return make_error_code(ret);
    }

    return {};
}

std::error_code SocketBase::SetPointerOption(const char *name,
                                             const void *value, size_t len) {
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
