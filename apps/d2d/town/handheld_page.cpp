// SPDX-License-Identifier: GPL-3.0-or-later
// A handheld's game menu and death message, as a page of its second screen (handheld_menu.hpp): the
// 800x600 frame's own, at two fifths, could not be read. The top screen keeps the world.
#include "town.hpp"

#include "common.hpp"
#include "game_api.hpp"
#include "handheld_menu.hpp"
#include "scene.hpp"

#include <string>
#include <utility>

namespace d2d::client {

// The death message is up (FUN_00453100's: view.cpp draws the frame's); not for local co-op's ghost while a partner lives.
auto Town::death_page() const -> bool {
        return g_world_only && view.pmode == kModeDD && !view.corpses.empty() && (!view.ghost || view.all_dead) && !game_menu.open && !controls.open;
    }

// What the second screen's page shows now: the game menu, else the death message; left inactive for neither.
auto Town::fill_handheld_page(MenuPage& page) const -> void {
        page.clear();
        if (!g_world_only || !scene || controls.open) return;
        if (game_menu.open) { game_menu.fill_page(page); return; }
        if (!death_page()) return;
        page.active = true;
        page.heading = "You have died";
        page.heading_ink = { 255, 77, 77 };
        page.note_ink = { 230, 200, 160 };
        const bool hardcore = view.header.hardcore();
        if (hardcore) page.notes.push_back(string_id(*scene, 0x13e8));           // "Your deeds of valor..."
        else if (view.gold_lost > 0) {                                           // "Death takes its toll of %d Gold"
            auto words = string_id(*scene, 0x13e6);
            if (const auto found = words.find("%d"); found != std::string::npos) words.replace(found, 2, std::to_string(view.gold_lost));
            page.notes.push_back(std::move(words));
        }
        if (!hardcore && view.header.active_difficulty() > 0) page.notes.push_back(string_id(*scene, 0x13e7));   // "You have lost experience"
        // Where the things went: the game leaves them on the body and says nothing (a new player read the weapon and
        // shield as gone: the emulator play test).
        if (!hardcore) page.notes.push_back("Your body keeps what you wore and carried. Go back to it and press A on it (Pick up corpse) to take it all back.");
        MenuPage::Row row;
        row.text = hardcore ? "Leave the Game" : "Continue";
        row.detail = hardcore ? "Touch here, or press A" : "Touch here, or press A, to rise in town";
        row.id = 0;
        row.on = true;
        page.rows.push_back(std::move(row));
    }

}  // namespace d2d::client
