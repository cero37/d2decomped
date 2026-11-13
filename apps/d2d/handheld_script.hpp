// SPDX-License-Identifier: GPL-3.0-or-later
// A test script: the dev channel's verbs from a file, a line a command, for
// driving the game where nothing can connect to the channel (the 3DS, an
// emulator with no keyboard). The file is $D2D_SCRIPT, else script.txt in
// $D2D_USER_DIR when that is set. Besides the channel's verbs:
//   wait <ms>        nothing for that long
//   # ...            a comment
// Each command and the first line of its reply go to the log ("script: ...").
#pragma once

namespace d2d::devctl { class Channel; }

namespace d2d::client {

// A script is running: it types with the `text` verb, so the platform's own on-screen keyboard
// (the 3DS's, which waits for a person) isn't asked for.
inline bool g_script_running = false;

// Once a frame from the main loop: runs the commands that are due.
void handheld_script_step(d2d::devctl::Channel& channel);

}  // namespace d2d::client
