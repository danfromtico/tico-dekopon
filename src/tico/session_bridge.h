// Copyright 2026 Dan | ticoverse.com
// SPDX-License-Identifier: GPL-2.0-or-later

// tico's sealed session (TicoSession.h) for Dekopon's own code. That header
// brings in libnx's switch.h, whose types clash with the core's (u128,
// Service), so only session_bridge.cpp includes it.
#pragma once

#include <string>

namespace TicoBridge {

/// tico::UserContentRoot: the current user's saves or states folder.
std::string UserContentRoot(const std::string& folder, bool saves);
/// tico::Session::inheritsShared: old data in tico/system/dekopon is this user's.
bool InheritsShared();
/// tico::SettingsText: tico's "general", "display" or "audio" settings.
std::string SettingsText(const char* name);
/// tico::PlaytimeFrame / PlaytimeFlush: the player's playtime.
void PlaytimeFrame();
void PlaytimeFlush();
/// tico::UseShaderCacheDir: Mesa's shader cache in @p dir, before the GPU starts.
void UseShaderCacheDir(const std::string& dir);

} // namespace TicoBridge
