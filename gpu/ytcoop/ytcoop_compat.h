// SPDX-License-Identifier: GPL-2.0-or-later
// BloodOnline (Yharnam Together module): what the co-op code needs from the C++ library that macOS
// before 26 lacks. std::from_chars for float and double is "introduced in macOS 26.0" in Apple's
// libc++, and the runtime targets macOS 15: the vendored sources call ytcoop::from_chars instead
// (vendor-changes.diff), which parses floating point with strtod in the "C" locale and forwards
// integers to std::from_chars.
#pragma once

#include <cerrno>
#include <charconv>
#include <clocale>
#include <cstdlib>
#include <string>
#include <system_error>
#include <type_traits>
#include <utility>
#if defined(__APPLE__)
#include <xlocale.h>
#endif

namespace ytcoop {

template <typename T, typename... Rest>
std::from_chars_result from_chars(const char* first, const char* last, T& value, Rest&&... rest) {
    if constexpr (std::is_floating_point_v<T>) {
        const std::string text(first, last); // strtod needs the end of the string
        char* end = nullptr;
        errno = 0;
#if defined(__APPLE__)
        static const locale_t c_locale = newlocale(LC_ALL_MASK, "C", nullptr);
        const double parsed = strtod_l(text.c_str(), &end, c_locale);
#else
        const double parsed = std::strtod(text.c_str(), &end);
#endif
        // std::from_chars takes no leading spaces or '+' (strtod does).
        if (end == text.c_str() || text.empty() || text[0] == ' ' || text[0] == '\t' ||
            text[0] == '+') {
            return {first, std::errc::invalid_argument};
        }
        const char* stop = first + (end - text.c_str());
        if (errno == ERANGE) {
            return {stop, std::errc::result_out_of_range};
        }
        value = static_cast<T>(parsed);
        return {stop, std::errc{}};
    } else {
        return std::from_chars(first, last, value, std::forward<Rest>(rest)...);
    }
}

} // namespace ytcoop
