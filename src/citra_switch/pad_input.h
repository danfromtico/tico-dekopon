// SPDX-FileCopyrightText: Azahar Emulator Project
// Copyright(c) 2026: PalindromicBreadLoaf (palindromicbreadloaf@tuta.com)
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <switch.h>

#include "citra_switch/input.h"

// The Switch pad, touch screen and motion sensors, read with libnx.
namespace SwitchFrontend {

// Starts and stops the six-axis sensors the motion input reads.
void StartSixAxis();
void StopSixAxis();

// Reads the pad into `state` and returns the raw buttons held mask.
// Forwarding the state to the guest is left to the caller so it can withhold input while a menu
// is up.
u64 PollInput(PadState& pad, InputState& state);

} // namespace SwitchFrontend
