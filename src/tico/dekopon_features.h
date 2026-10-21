// Copyright 2026 Dan | ticoverse.com
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

namespace Core {
class System;
}

// Dekopon's in-game extras in tico's menu: cheats, and Amiibo and the camera pictures, which
// share the menu's live list (its Portal screen, titled "Amiibo & Camera").
namespace SwitchFrontend::DekoponFeatures {

// For the game that just started.
void Register(Core::System& system, unsigned long long title_id);

// The game's cheats as they are now, written back to its cheat file.
void SaveCheats(Core::System& system, unsigned long long title_id);

} // namespace SwitchFrontend::DekoponFeatures
