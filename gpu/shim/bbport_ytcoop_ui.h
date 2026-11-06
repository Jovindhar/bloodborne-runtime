// SPDX-License-Identifier: GPL-2.0-or-later
// BloodOnline: the Yharnam Together screens over the game (bbport_ytcoop_ui.cpp). The overlay
// calls these under its ImGui lock; all are no-ops unless the online module is Yharnam Together's.
#pragma once

namespace BbYtCoop {
/// The loaded online module is Yharnam Together's.
bool Available();
/// Present thread, once a frame before Visible/Draw: reads the module's screens.
void Update();
/// A team screen, its loading rows or a travel question is up.
bool WantsDraw();
/// The game's input is held while the team screen (or a travel question) is up.
bool CapturesInput();
/// Opens the team screen in game (the menu's button).
void OpenTeam();
/// Escape: closes the team screen opened in game (not the title's, which waits for a choice).
void Close();
/// Inside the overlay's ImGui frame.
void Draw();
} // namespace BbYtCoop
