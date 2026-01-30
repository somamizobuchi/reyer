#include "reyer_rt/net/message_types.hpp"
#include <glaze/glaze.hpp>
#include <glaze/json/write.hpp>
#include <glaze/json/read.hpp>
#include <iostream>
#include <variant>
#include <iomanip>
#include <cassert>

using MessageVariant = std::variant<
    reyer_rt::net::message::Ping,
    reyer_rt::net::message::ProtocolRequest,
    reyer_rt::net::message::CommandRequest
>;

void print_json(const std::string& data) {
    std::cout << "JSON: " << data << std::endl;
}

int main() {
    std::cout << "=== Testing JSON Variant Serialization ===" << std::endl;

    // Create a Ping message
    reyer_rt::net::message::Ping ping{12345};

    // Test 1: Serialize the Ping directly (not in variant)
    std::cout << "\n[Test 1] Serializing Ping directly..." << std::endl;
    std::string ping_buffer;
    auto err1 = glz::write_json(ping, ping_buffer);
    assert(!err1 && "Failed to serialize Ping directly");
    assert(!ping_buffer.empty() && "Ping buffer should not be empty");

    std::cout << "  ✓ Ping serialized directly" << std::endl;
    std::cout << "    Length: " << ping_buffer.size() << " bytes" << std::endl;
    std::cout << "    ";
    print_json(ping_buffer);

    // Test 2: Serialize as a variant
    std::cout << "\n[Test 2] Serializing Ping as variant..." << std::endl;
    MessageVariant variant_ping = ping;
    std::string variant_buffer;
    auto err2 = glz::write_json(variant_ping, variant_buffer);
    assert(!err2 && "Failed to serialize variant");
    assert(!variant_buffer.empty() && "Variant buffer should not be empty");

    std::cout << "  ✓ Ping serialized as variant" << std::endl;
    std::cout << "    Length: " << variant_buffer.size() << " bytes" << std::endl;
    std::cout << "    ";
    print_json(variant_buffer);

    // Test 3: Verify that direct and variant serializations are different
    std::cout << "\n[Test 3] Verifying format difference..." << std::endl;
    assert(ping_buffer != variant_buffer && "Direct and variant serialization should differ");
    std::cout << "  ✓ Direct and variant serializations are different (as expected)" << std::endl;

    // Test 4: Deserialize variant buffer back to variant
    std::cout << "\n[Test 4] Deserializing variant buffer..." << std::endl;
    MessageVariant deserialized_from_variant;
    auto err3 = glz::read_json(deserialized_from_variant, variant_buffer);
    assert(!err3 && "Failed to deserialize variant buffer");
    assert(deserialized_from_variant.index() == 0 && "Variant should hold Ping (index 0)");
    assert(std::holds_alternative<reyer_rt::net::message::Ping>(deserialized_from_variant));

    auto& ping_from_variant = std::get<reyer_rt::net::message::Ping>(deserialized_from_variant);
    assert(ping_from_variant.timestamp == 12345 && "Timestamp should match");

    std::cout << "  ✓ Variant deserialized successfully" << std::endl;
    std::cout << "    Variant index: " << deserialized_from_variant.index() << std::endl;
    std::cout << "    Ping timestamp: " << ping_from_variant.timestamp << std::endl;

    // Test 5: Try to deserialize direct Ping buffer as variant (should work with JSON auto-deduction!)
    std::cout << "\n[Test 5] Attempting to deserialize direct Ping as variant (should work with JSON)..." << std::endl;
    MessageVariant deserialized_variant;
    auto err4 = glz::read_json(deserialized_variant, ping_buffer);

    if (err4) {
        std::cout << "  ✗ Failed: " << err4.custom_error_message << std::endl;
        std::cout << "    Unexpected - JSON should support variant auto-deduction!" << std::endl;
        return 1;
    } else {
        std::cout << "  ✓ SUCCESS: Direct Ping deserialized as variant with auto-deduction!" << std::endl;
        std::cout << "    Variant index: " << deserialized_variant.index() << std::endl;
        if (auto* p = std::get_if<reyer_rt::net::message::Ping>(&deserialized_variant)) {
            std::cout << "    Ping timestamp: " << p->timestamp << std::endl;
            assert(p->timestamp == 12345 && "Timestamp should match");
        }
    }

    std::cout << "\n=== All tests passed! ===" << std::endl;
    std::cout << "\nKey takeaway: With JSON, clients can send plain structs!" << std::endl;
    std::cout << "Glaze automatically deduces the correct variant type." << std::endl;
    std::cout << "For Ping, just send: {\"timestamp\": <value>}" << std::endl;

    return 0;
}
