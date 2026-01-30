#include "reyer_rt/net/message_types.hpp"
#include "reyer_rt/net/message_glaze_meta.hpp"
#include <glaze/glaze.hpp>
#include <glaze/msgpack/write.hpp>
#include <glaze/msgpack/read.hpp>
#include <iostream>
#include <variant>
#include <iomanip>
#include <cassert>

using MessageVariant = std::variant<
    reyer_rt::net::message::Ping,
    reyer_rt::net::message::ProtocolRequest
>;

void print_hex(const std::string& data) {
    std::cout << "Hex: ";
    for (unsigned char c : data) {
        std::cout << std::hex << std::setw(2) << std::setfill('0') << (int)c;
    }
    std::cout << std::dec << std::endl;
}

int main() {
    std::cout << "=== Testing MessagePack Variant Auto-Deduction ===" << std::endl;

    // Serialize a plain Ping (without variant wrapper)
    reyer_rt::net::message::Ping ping{12345};
    std::string ping_buffer;
    auto err = glz::write_msgpack(ping, ping_buffer);
    if (err) {
        std::cerr << "Failed to serialize Ping: " << err.custom_error_message << std::endl;
        return 1;
    }

    std::cout << "\nSerialized plain Ping:" << std::endl;
    std::cout << "  Length: " << ping_buffer.size() << " bytes" << std::endl;
    print_hex(ping_buffer);

    // Try to read it back as a variant with auto-deduction
    std::cout << "\nAttempting to read plain Ping into variant (auto-deduction)..." << std::endl;
    MessageVariant variant;

    // Method 1: Direct read
    auto err1 = glz::read_msgpack(variant, ping_buffer);
    if (err1) {
        std::cout << "  Method 1 (direct read) failed: " << err1.custom_error_message << std::endl;
    } else {
        std::cout << "  Method 1 (direct read) SUCCESS!" << std::endl;
        std::cout << "    Variant index: " << variant.index() << std::endl;
        if (auto* p = std::get_if<reyer_rt::net::message::Ping>(&variant)) {
            std::cout << "    Ping timestamp: " << p->timestamp << std::endl;
        }
    }

    // Check if we need to use the tagged variant approach
    std::cout << "\nNote: MessagePack variant auto-deduction may not be supported in glaze." << std::endl;
    std::cout << "The documentation only mentions JSON variant auto-deduction." << std::endl;

    std::cout << "\n=== Conclusion ===" << std::endl;
    if (err1) {
        std::cout << "Auto-deduction is NOT working for MessagePack variants in glaze v7.0.1" << std::endl;
        std::cout << "Client must send variant format: [index, value]" << std::endl;
        return 1;
    } else {
        std::cout << "Auto-deduction WORKS! Client can send plain structs." << std::endl;
        return 0;
    }
}
