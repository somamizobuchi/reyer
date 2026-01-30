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
    for (unsigned char c : data) {
        std::cout << std::hex << std::setw(2) << std::setfill('0') << (int)c;
    }
    std::cout << std::dec << std::endl;
}

int main() {
    std::cout << "=== Testing MessagePack Variant Serialization ===" << std::endl;

    // Create a Ping message
    reyer_rt::net::message::Ping ping{12345};

    // Test 1: Serialize the Ping directly (not in variant)
    std::cout << "\n[Test 1] Serializing Ping directly..." << std::endl;
    std::string ping_buffer;
    auto err1 = glz::write_msgpack(ping, ping_buffer);
    assert(!err1 && "Failed to serialize Ping directly");
    assert(!ping_buffer.empty() && "Ping buffer should not be empty");

    std::cout << "  ✓ Ping serialized directly" << std::endl;
    std::cout << "    Length: " << ping_buffer.size() << " bytes" << std::endl;
    std::cout << "    Hex: ";
    print_hex(ping_buffer);

    // Test 2: Serialize as a variant
    std::cout << "\n[Test 2] Serializing Ping as variant..." << std::endl;
    MessageVariant variant_ping = ping;
    std::string variant_buffer;
    auto err2 = glz::write_msgpack(variant_ping, variant_buffer);
    assert(!err2 && "Failed to serialize variant");
    assert(!variant_buffer.empty() && "Variant buffer should not be empty");

    std::cout << "  ✓ Ping serialized as variant" << std::endl;
    std::cout << "    Length: " << variant_buffer.size() << " bytes" << std::endl;
    std::cout << "    Hex: ";
    print_hex(variant_buffer);

    // Test 3: Verify that direct and variant serializations are different
    std::cout << "\n[Test 3] Verifying format difference..." << std::endl;
    assert(ping_buffer != variant_buffer && "Direct and variant serialization should differ");
    std::cout << "  ✓ Direct and variant serializations are different (as expected)" << std::endl;

    // Test 4: Deserialize variant buffer back to variant
    std::cout << "\n[Test 4] Deserializing variant buffer..." << std::endl;
    MessageVariant deserialized_from_variant;
    auto err3 = glz::read_msgpack(deserialized_from_variant, variant_buffer);
    assert(!err3 && "Failed to deserialize variant buffer");
    assert(deserialized_from_variant.index() == 0 && "Variant should hold Ping (index 0)");
    assert(std::holds_alternative<reyer_rt::net::message::Ping>(deserialized_from_variant));

    auto& ping_from_variant = std::get<reyer_rt::net::message::Ping>(deserialized_from_variant);
    assert(ping_from_variant.timestamp == 12345 && "Timestamp should match");

    std::cout << "  ✓ Variant deserialized successfully" << std::endl;
    std::cout << "    Variant index: " << deserialized_from_variant.index() << std::endl;
    std::cout << "    Ping timestamp: " << ping_from_variant.timestamp << std::endl;

    // Test 5: Try to deserialize direct Ping buffer as variant (should fail)
    std::cout << "\n[Test 5] Attempting to deserialize direct Ping as variant (should fail)..." << std::endl;
    MessageVariant deserialized_variant;
    auto err4 = glz::read_msgpack(deserialized_variant, ping_buffer);

    if (err4) {
        std::cout << "  ✓ Failed as expected: " << err4.custom_error_message << std::endl;
        std::cout << "    This demonstrates why Python must send variant format!" << std::endl;
    } else {
        std::cerr << "  ✗ UNEXPECTED: Direct Ping deserialized as variant" << std::endl;
        std::cerr << "    This may indicate glaze has changed its variant serialization" << std::endl;
        return 1;
    }

    std::cout << "\n=== All tests passed! ===" << std::endl;
    std::cout << "\nKey takeaway: Clients must send messages as variant format [index, value]" << std::endl;
    std::cout << "For Ping (first in variant), send: [0, {\"timestamp\": <value>}]" << std::endl;

    return 0;
}
