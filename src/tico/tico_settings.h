// Copyright 2026 Azahar Emulator Project
// Copyright 2026 Dan | ticoverse.com
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

namespace Core {
class System;
}

// tico's settings for Dekopon (tico/module/settings.json, the dekopon_* keys) applied to
// Dekopon's own. A key names a setting of Dekopon's registry (dekopon_<its config.ini key>) and
// holds its value as config.ini spells it, so Dekopon reads it exactly as it reads its own file;
// a few are the frontend's own (layout, controls, the console's profile, Boost mode).
namespace SwitchFrontend::TicoSettings {

// Every option, before the game boots.
void Apply();

// After a change in the quick menu, or a dock change: the options that take effect in game.
void ApplyLive();

// Once the game is loaded, on the emulation thread: what needs it running (the console's name
// and language, written to its config save, and the screen layout).
void ApplyLoaded(Core::System& system);

// Boost mode (Settings > System), for the whole game.
bool BoostMode();

} // namespace SwitchFrontend::TicoSettings
