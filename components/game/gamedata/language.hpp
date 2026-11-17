// SPDX-License-Identifier: GPL-3.0-or-later
// Which language's files d2d reads: the string tables (data\local\LNG\<code>\)
// and the menu art (data\local\UI\<code>\). game.exe takes the code from the
// Windows locale; a handheld has no registry, so d2d.cfg's `language =` says
// it, else the data the install has decides. No dependencies, so a test
// builds it alone (tests/test_language.cpp).
#pragma once

#include <cctype>
#include <functional>
#include <string>
#include <string_view>

namespace d2d::game {

// The code whose files load. `wanted` (d2d.cfg `language =`, any case) wins
// when `has_string_table` finds its string.tbl; otherwise ENG when it has
// one, then ESN (Spanish). A data set with neither keeps ENG, the code d2d
// always used, so an English install behaves as it did.
inline std::string choose_language(std::string_view wanted, const std::function<bool(const std::string&)>& has_string_table) {
    std::string code;
    for (const char letter : wanted) code.push_back(char(std::toupper(static_cast<unsigned char>(letter))));
    if (!code.empty() && has_string_table(code)) return code;
    for (const char* fallback : { "ENG", "ESN" })
        if (has_string_table(fallback)) return fallback;
    return "ENG";
}

}  // namespace d2d::game
