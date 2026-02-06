#include <cstdint>

namespace reyer::core {
// FNV-1a hash
constexpr uint64_t hash_string(const char* str) {
    uint64_t hash = 14695981039346656037ULL;
    while (*str) {
        hash ^= static_cast<uint64_t>(*str++);
        hash *= 1099511628211ULL;
    }
    return hash;
}

}
