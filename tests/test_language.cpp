// SPDX-License-Identifier: GPL-3.0-or-later
// test-links:
// test-includes: components/game/gamedata
// Which language's string tables load (components/game/gamedata/language.hpp):
// d2d.cfg's choice when the install has it, else ENG, else ESN, else ENG.
#include <language.hpp>

#include <cstdio>
#include <set>
#include <string>
#include <utility>

namespace {

int failures = 0;
int cases = 0;

// An install with exactly these string tables.
auto install(std::set<std::string> codes) {
    return [codes = std::move(codes)](const std::string& code) { return codes.contains(code); };
}

void expect(const char* what, const std::string& got, const std::string& want) {
    ++cases;
    if (got == want) return;
    std::printf("FAIL: %s: got %s, want %s\n", what, got.c_str(), want.c_str());
    ++failures;
}

}  // namespace

int main() {
    using d2d::game::choose_language;

    // A Spanish disc: only ESN, no setting. The Spanish tables load.
    expect("Spanish-only install, no setting", choose_language("", install({ "ESN" })), "ESN");

    // An English disc: only ENG. Unchanged from before.
    expect("English-only install, no setting", choose_language("", install({ "ENG" })), "ENG");

    // Both installed: English stays the default, as it always was.
    expect("both installed, no setting", choose_language("", install({ "ENG", "ESN" })), "ENG");

    // d2d.cfg picks Spanish over the English default.
    expect("setting ESN wins over ENG", choose_language("ESN", install({ "ENG", "ESN" })), "ESN");

    // The setting is case-insensitive.
    expect("lowercase setting", choose_language("esn", install({ "ENG", "ESN" })), "ESN");

    // A setting the install lacks falls back to the data it has.
    expect("setting missing from install", choose_language("DEU", install({ "ESN" })), "ESN");

    // Nothing at all: ENG, so the tables come up empty as before.
    expect("no tables at all", choose_language("", install({})), "ENG");
    expect("no tables, setting given", choose_language("ESN", install({})), "ENG");

    if (failures == 0) std::printf("PASS: test_language (%d cases)\n", cases);
    return failures == 0 ? 0 : 1;
}
