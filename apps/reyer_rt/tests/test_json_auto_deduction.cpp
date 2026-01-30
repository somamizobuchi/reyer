#include "reyer_rt/net/message_types.hpp"
#include <glaze/glaze.hpp>
#include <glaze/json/write.hpp>
#include <glaze/json/read.hpp>
#include <iostream>
#include <variant>
#include <cassert>

using MessageVariant = std::variant<
    reyer_rt::net::message::Ping,
    reyer_rt::net::message::ProtocolRequest
>;

void print_json(const std::string& data) {
    std::cout << "JSON: " << data << std::endl;
}

int main() {
    std::cout << "=== Testing JSON Variant Auto-Deduction ===" << std::endl;

    // Serialize a plain Ping (without variant wrapper)
    reyer_rt::net::message::Ping ping{12345};
    std::string ping_buffer;
    auto err = glz::write_json(ping, ping_buffer);
    if (err) {
        std::cerr << "Failed to serialize Ping: " << err.custom_error_message << std::endl;
        return 1;
    }

    std::cout << "\nSerialized plain Ping:" << std::endl;
    std::cout << "  Length: " << ping_buffer.size() << " bytes" << std::endl;
    std::cout << "  ";
    print_json(ping_buffer);

    // Try to read it back as a variant with auto-deduction
    std::cout << "\nAttempting to read plain Ping into variant (auto-deduction)..." << std::endl;
    MessageVariant variant;

    // Direct read with auto-deduction
    auto err1 = glz::read_json(variant, ping_buffer);
    if (err1) {
        std::cout << "  ✗ Auto-deduction failed: " << err1.custom_error_message << std::endl;
        std::cout << "\n=== Conclusion ===" << std::endl;
        std::cout << "Auto-deduction is NOT working for JSON variants" << std::endl;
        std::cout << "Client must send variant format: [index, value]" << std::endl;
        return 1;
    } else {
        std::cout << "  ✓ Auto-deduction SUCCESS!" << std::endl;
        std::cout << "    Variant index: " << variant.index() << std::endl;
        if (auto* p = std::get_if<reyer_rt::net::message::Ping>(&variant)) {
            std::cout << "    Ping timestamp: " << p->timestamp << std::endl;
            assert(p->timestamp == 12345 && "Timestamp should match");
        }
    }

    std::cout << "\n=== Conclusion ===" << std::endl;
    std::cout << "✓ Auto-deduction WORKS! Client can send plain structs." << std::endl;
    std::cout << "  Glaze automatically determines the correct variant type for JSON." << std::endl;
    std::cout << "  Clients just send: {\"timestamp\": 12345}" << std::endl;
    std::cout << "  Server receives: variant<Ping, ...> with Ping automatically selected." << std::endl;

    return 0;
}
