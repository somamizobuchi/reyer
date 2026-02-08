#pragma once

#include <concepts>
#include <numbers>
#include <cstdint>
#include <cmath>

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

double deg2rad(const DoubleConvertible auto degrees) {
    return static_cast<double>(degrees) * std::numbers::pi / 180.0;
}

double rad2deg(const DoubleConvertible auto radians) {
    return static_cast<double>(radians) * 180.0 / std::numbers::pi;
}

double calculatePPD(const DoubleConvertible auto pixels, const DoubleConvertible auto size, const DoubleConvertible auto distance) {
    auto angle_degrees = rad2deg(std::atan(static_cast<double>(size) / (2.0 * static_cast<double>(distance))));
    return static_cast<double>(pixels) / 2.0 / angle_degrees;
}

} // namespace reyer::core
