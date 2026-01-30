#pragma once

#include <cstddef>
#include <nng/nng.h>
#include <vector>
#include <cstdint>
#include <system_error>
#include <memory>

namespace reyer_rt::net::detail {

// Socket type determines which NNG protocol to use
enum class SocketType {
    REP,  // Reply (server-side REQ/REP)
    REQ,  // Request (client-side REQ/REP)
    PUB,  // Publish (server-side PUB/SUB)
    SUB,  // Subscribe (client-side PUB/SUB)
};

// RAII wrapper for NNG messages
class MessagePtr {
public:
    explicit MessagePtr(nng_msg* msg = nullptr) : msg_(msg) {}

    ~MessagePtr() {
        if (msg_) {
            nng_msg_free(msg_);
        }
    }

    // Non-copyable
    MessagePtr(const MessagePtr&) = delete;
    MessagePtr& operator=(const MessagePtr&) = delete;

    // Movable
    MessagePtr(MessagePtr&& other) noexcept : msg_(other.release()) {}
    MessagePtr& operator=(MessagePtr&& other) noexcept {
        reset(other.release());
        return *this;
    }

    nng_msg* get() const { return msg_; }
    nng_msg* operator->() const { return msg_; }
    nng_msg& operator*() const { return *msg_; }

    nng_msg* release() {
        auto ptr = msg_;
        msg_ = nullptr;
        return ptr;
    }

    void reset(nng_msg* msg = nullptr) {
        if (msg_) {
            nng_msg_free(msg_);
        }
        msg_ = msg;
    }

private:
    nng_msg* msg_ = nullptr;
};

// Shared NNG socket implementation
class SocketBase {
public:
    SocketBase();
    ~SocketBase();

    // Non-copyable, non-movable (manages NNG socket lifecycle)
    SocketBase(const SocketBase&) = delete;
    SocketBase& operator=(const SocketBase&) = delete;
    SocketBase(SocketBase&&) = delete;
    SocketBase& operator=(SocketBase&&) = delete;

    // Initialize socket with specified type
    std::error_code Init(SocketType type);

    // Bind to address (server-side)
    std::error_code Bind(const std::string& address);

    // Connect to address (client-side)
    std::error_code Connect(const std::string& address);

    // Send message (takes ownership via nng_sendmsg)
    std::error_code Send(const std::string& data);

    // Receive message
    std::error_code Receive(std::string& data);

    // Close socket
    void Close();

    // Check if socket is open
    bool IsOpen() const;

    // Set integer socket option
    std::error_code SetIntOption(const char* name, int value);

    // Set pointer socket option (for options with data)
    std::error_code SetPointerOption(const char* name, const void* value, size_t len);

private:
    nng_socket socket_ = NNG_SOCKET_INITIALIZER;
    nng_listener listener_ = NNG_LISTENER_INITIALIZER;
    nng_dialer dialer_ = NNG_DIALER_INITIALIZER;
    bool is_open_ = false;
};

// Error category for NNG
class nng_error_category : public std::error_category {
public:
    const char* name() const noexcept override {
        return "nng";
    }

    std::string message(int ev) const override {
        return nng_strerror(ev);
    }
};

inline const std::error_category& nng_category() {
    static nng_error_category instance;
    return instance;
}

inline std::error_code make_error_code(int nng_errno) {
    return std::error_code(nng_errno, nng_category());
}

} // namespace reyer_rt::net::detail
