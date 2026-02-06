#include <concepts>
#include <cstdint>

namespace reyer::core {
// FNV-1a hash
constexpr uint64_t hash_string(const char *str) {
    uint64_t hash = 14695981039346656037ULL;
    while (*str) {
        hash ^= static_cast<uint64_t>(*str++);
        hash *= 1099511628211ULL;
    }
    return hash;
}

template <typename T>
concept DoubleConvertible = std::convertible_to<T, double>;

double deg2rad(DoubleConvertible degrees) {
    return static_cast<double>(degrees) * std::numbers::pi / 180.0;
}

double rad2deg(DoubleConvertible radians) {
    return static_cast<double>(radians) * 180.0 / std::numbers::pi;
}

} // namespace reyer::core
