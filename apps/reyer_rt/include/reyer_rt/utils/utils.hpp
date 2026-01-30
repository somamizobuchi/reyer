#include <cstdarg>
#include <cstdio>
#include <string>
#include <array>
#include <random>
#include <sstream>
#include <iomanip>

namespace reyer_rt::utils {

inline std::string vprintf_to_string(const char* fmt, va_list args)
{
    va_list args_copy;
    va_copy(args_copy, args);

    int size = std::vsnprintf(nullptr, 0, fmt, args_copy);
    va_end(args_copy);

    if (size < 0)
        return {};

    std::string result(size, '\0');
    std::vsnprintf(result.data(), result.size() + 1, fmt, args);

    return result;
}


inline std::string uuid_v4()
{
    static std::random_device rd;
    static std::mt19937_64 gen(rd());
    static std::uniform_int_distribution<uint64_t> dis;

    std::array<uint8_t, 16> bytes;
    for (int i = 0; i < 16; i += 8) {
        uint64_t r = dis(gen);
        std::memcpy(bytes.data() + i, &r, 8);
    }

    // Set version (4) and variant (RFC4122)
    bytes[6] = (bytes[6] & 0x0F) | 0x40;
    bytes[8] = (bytes[8] & 0x3F) | 0x80;

    std::ostringstream ss;
    for (int i = 0; i < 16; ++i) {
        if (i == 4 || i == 6 || i == 8 || i == 10) ss << '-';
        ss << std::hex << std::setw(2) << std::setfill('0') << (int)bytes[i];
    }
    return ss.str();
}

} // namespace reyer_rt::utils
