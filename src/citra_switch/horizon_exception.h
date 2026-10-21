// SPDX-FileCopyrightText: Azahar Emulator Project
// Copyright(c) 2026: PalindromicBreadLoaf(palindromicbreadloaf@tuta.com)
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

// Where the crash handler writes crash.txt: a folder on the SD card such as
// "sdmc:/switch/dekopon/log/". Set once at startup; the handler itself cannot build paths.
void SetCrashLogDirectory(const char* directory);
