// SPDX-FileCopyrightText: Azahar Emulator Project
// Copyright(c) 2026: PalindromicBreadLoaf (palindromicbreadloaf@tuta.com)
// SPDX-License-Identifier: GPL-2.0-or-later

#include <array>
#include <cstdio>
#include <string>
#include <utility>
#include <switch.h>

#include "citra_switch/amiibo_session.h"
#include "citra_switch/applets/swkbd.h"
#include "citra_switch/updater.h"
#include "citra_switch/config.h"
#include "citra_switch/input.h"
#include "citra_switch/menu.h"
#include "citra_switch/overlay_menu.h"
#include "citra_switch/pad_input.h"
#include "citra_switch/usb_storage.h"
#include "common/horizon_thread.h"

namespace Common {
void StopAllThreadWorkers();
}

extern "C" {
u32 __nx_applet_type = AppletType_Application;
size_t __nx_heap_size = 0;
}

namespace {

void RunGame(PadState& pad, const std::string& rom) {
    if (!SwitchFrontend::CreateWindow(nwindowGetDefault())) {
        std::printf("EmuWindow no worky.\n");
        SwitchFrontend::SetMenuNotice("Couldn't create the render window");
        return;
    }

    // Each game always starts with the touch pointer off and the quick menu closed.
    SwitchFrontend::ResetPointer();

    if (SwitchFrontend::BootRom(rom)) {
        u64 prev_held = 0;
        u64 prev_input = 0;
        while (appletMainLoop()) {
            // Blocks
            SwitchFrontend::PumpKeyboard();

            SwitchFrontend::UpdateDisplayMode();

            SwitchFrontend::InputState state;
            const u64 held = SwitchFrontend::PollInput(pad, state);
            const u64 pressed = held & ~prev_held;
            // Emulator actions are edge-detected in the remappable InputButton space.
            const u64 input_pressed = state.buttons & ~prev_input;

            // The +/- chord toggles the in-game quick menu.
            constexpr u64 chord = HidNpadButton_Plus | HidNpadButton_Minus;
            const bool chord_edge = (held & chord) == chord && (prev_held & chord) != chord;
            if (chord_edge) {
                SwitchFrontend::ToggleQuickMenu();
            }

            const bool menu_open = SwitchFrontend::IsQuickMenuOpen();

            // While the menu is up the guest sees neutral input.
            SwitchFrontend::UpdateInput(menu_open || chord_edge ? SwitchFrontend::InputState{}
                                                                : state);

            if (menu_open && !chord_edge) {
                const SwitchFrontend::MenuDirections dpad = SwitchFrontend::RotateMenuDirections({
                    .up = (pressed & HidNpadButton_Up) != 0,
                    .down = (pressed & HidNpadButton_Down) != 0,
                    .left = (pressed & HidNpadButton_Left) != 0,
                    .right = (pressed & HidNpadButton_Right) != 0,
                });
                const SwitchFrontend::MenuDirections stick = SwitchFrontend::RotateMenuDirections({
                    .up = (pressed & HidNpadButton_StickLUp) != 0,
                    .down = (pressed & HidNpadButton_StickLDown) != 0,
                    .left = (pressed & HidNpadButton_StickLLeft) != 0,
                    .right = (pressed & HidNpadButton_StickLRight) != 0,
                });
                const SwitchFrontend::QuickMenuNav nav{
                    .up = dpad.up || stick.up,
                    .down = dpad.down || stick.down,
                    .left = dpad.left,
                    .right = dpad.right,
                    .confirm = (pressed & HidNpadButton_A) != 0,
                    .cancel = (pressed & HidNpadButton_B) != 0,
                    .alt = (pressed & HidNpadButton_X) != 0,
                    .alt2 = (pressed & HidNpadButton_Y) != 0,
                    .tab_prev = (pressed & HidNpadButton_L) != 0,
                    .tab_next = (pressed & HidNpadButton_R) != 0,
                    .page_prev = (pressed & HidNpadButton_ZL) != 0,
                    .page_next = (pressed & HidNpadButton_ZR) != 0,
                };
                if (SwitchFrontend::UpdateQuickMenu(nav) ==
                    SwitchFrontend::QuickMenuAction::ExitGame) {
                    break;
                }
            } else if (!menu_open) {
                using SwitchFrontend::ButtonMask;
                using SwitchFrontend::GetMapping;
                using SwitchFrontend::MappableControl;
                // The button bound to Cycle Screen Layout (R3 by default) steps the layout.
                if ((input_pressed & ButtonMask(GetMapping(MappableControl::CycleLayout))) != 0) {
                    SwitchFrontend::CycleScreenLayout();
                }
                // The button bound to Toggle Touch Pointer (L3 by default) toggles pointer mode.
                if ((input_pressed & ButtonMask(GetMapping(MappableControl::TogglePointer))) != 0) {
                    SwitchFrontend::TogglePointerMode();
                }
                // The button bound to Swap Screens (unbound by default) flips the two screens.
                if ((input_pressed & ButtonMask(GetMapping(MappableControl::SwapScreens))) != 0) {
                    SwitchFrontend::ToggleSwapScreens();
                }
            }

            SwitchFrontend::UpdateRealAmiibo();

            prev_held = held;
            prev_input = state.buttons;
            if (!SwitchFrontend::IsRunning()) {
                break;
            }
            // The async GPU thread shares core 0 with this loop.
            svcSleepThread(4'000'000);
        }
        SwitchFrontend::EndRealAmiibo();
        SwitchFrontend::StopRom();
        if (SwitchFrontend::ArticDisconnected()) {
            SwitchFrontend::SetMenuNotice(
                "Artic connection failed. Please check the address and tool/server version");
        } else if (SwitchFrontend::LoadFailed()) {
            SwitchFrontend::SetMenuNotice("Couldn't launch. Please check keys/ROM");
        } else if (rom.starts_with("articinio://") || rom.starts_with("articinin://")) {
            const auto installed = SwitchFrontend::GetSystemFileSetupState();
            const bool complete =
                rom.starts_with("articinio://") ? installed.old3ds : installed.new3ds;
            SwitchFrontend::SetMenuNotice(
                complete ? "System-file setup completed"
                         : "System-file setup stopped before all files were installed",
                !complete);
        }
    } else {
        SwitchFrontend::SetMenuNotice("Couldn't launch. ROM not loadable");
    }

    // Make sure a lingering overlay never survives into the next game or the library menu.
    SwitchFrontend::CloseQuickMenu();
    SwitchFrontend::DestroyWindow();
}

} // namespace

int main(int argc, char* argv[]) {
    SwitchFrontend::SetUpdaterExecutablePath(argc > 0 && argv[0] != nullptr ? argv[0] : "");
    const bool have_socket = R_SUCCEEDED(socketInitializeDefault());
    if (have_socket) {
        nxlinkStdio();
    }
    // Mount the embedded romfs for deko3D shaders
    const bool have_romfs = R_SUCCEEDED(romfsInit());
    if (!have_romfs) {
        std::printf("Warning: romfsInit() failed.\n");
    }
    if (!Common::Horizon::PinCurrentThread(Common::Horizon::CoreFrontend)) {
        std::printf("Warning: failed to pin frontend thread to core 0.\n");
    }

    if (!SwitchFrontend::InitUsbStorage()) {
        std::printf("Warning: %s\n", SwitchFrontend::UsbStorageError().c_str());
    }

    // Resolve SD-card dirs and create folders/files if not present
    const int launch_count = SwitchFrontend::Bootstrap();
    std::printf("FS & logging up (launch #%d). Logs are located at sdmc:/switch/dekopon/log/\n",
                launch_count);

    padConfigureInput(1, HidNpadStyleSet_NpadStandard);
    PadState pad;
    padInitializeDefault(&pad);
    hidInitializeTouchScreen();
    SwitchFrontend::StartSixAxis();

    SwitchFrontend::InitializeInput();

    std::string pending_rom = (argc > 1 && argv[1] != nullptr) ? argv[1] : std::string{};

    while (appletMainLoop()) {
        std::string rom;
        if (!pending_rom.empty()) {
            rom = std::move(pending_rom);
            pending_rom.clear();
        } else {
            const SwitchFrontend::MenuResult choice = SwitchFrontend::RunMenu(pad);
            if (choice.action == SwitchFrontend::MenuAction::Exit) {
                break;
            }
            rom = choice.path;
        }

        if (!rom.empty()) {
            RunGame(pad, rom);
        }
    }

    SwitchFrontend::ShutdownMenu();
    SwitchFrontend::ShutdownInput();
    SwitchFrontend::StopSixAxis();
    SwitchFrontend::Shutdown();
    Common::StopAllThreadWorkers();
    SwitchFrontend::ShutdownUsbStorage();
    if (have_romfs) {
        romfsExit();
    }
    if (have_socket) {
        socketExit();
    }
    return 0;
}
