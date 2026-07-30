#pragma once

#include <string>
#include <format>
#include <cstdint>
#include <cstdarg>

#ifdef PLATFORM_LINUX
#include <String/StringType.hpp>
#endif

namespace PS {

#ifdef PLATFORM_LINUX
    // Convert char16_t format string to char for std::format compatibility
    inline std::string Char16ToChar(const RC::CharType* str) {
        if constexpr (std::is_same_v<RC::CharType, char16_t>) {
            std::string result;
            result.reserve(64);
            for (; *str; ++str) {
                char16_t c = *str;
                if (c < 0x80) {
                    result += static_cast<char>(c);
                } else if (c < 0x800) {
                    result += static_cast<char>(0xC0 | (c >> 6));
                    result += static_cast<char>(0x80 | (c & 0x3F));
                } else {
                    result += static_cast<char>(0xE0 | (c >> 12));
                    result += static_cast<char>(0x80 | ((c >> 6) & 0x3F));
                    result += static_cast<char>(0x80 | (c & 0x3F));
                }
            }
            return result;
        } else {
            return std::string(reinterpret_cast<const char*>(str));
        }
    }

    template<typename... Args>
    auto Format(const RC::CharType* fmt, Args&&... args) {
        auto fmt_utf8 = Char16ToChar(fmt);
        return std::vformat(fmt_utf8, std::make_format_args(args...));
    }
#else
    template<typename... Args>
    auto Format(const wchar_t* fmt, Args&&... args) {
        return std::format(fmt, args...);
    }
#endif

} // namespace PS
