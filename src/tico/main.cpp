// Copyright Citra Emulator Project / Azahar Emulator Project
// Copyright 2026 Dan | ticoverse.com
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

// tico-dekopon: Dekopon started by tico with a game, tico's overlay as its menu, and back to
// tico when the game closes. Dekopon's frontend runs the game as dekopon.nro does (its
// emulation thread, renderer, input); this file only takes tico's place around it.

// before anything that includes <switch.h>: libnx's names clash with the core's
#include "tico/switch_libnx.h"

#include <array>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <exception>
#include <string>
#include <thread>
#include <vector>
#include <pthread.h>
#include <sys/stat.h>

#include "UsbStorage.h"
#include "citra_switch/amiibo_session.h"
#include "citra_switch/applets/swkbd.h"
#include "citra_switch/config.h"
#include "citra_switch/input.h"
#include "citra_switch/pad_input.h"
#include "common/horizon_thread.h"
#include "common/logging/log.h"
#include "common/settings.h"
#include "core/core.h"
#include "core/hle/kernel/kernel.h"
#include "core/loader/loader.h"
#include "overlay/overlay_ui.h"
#include "overlay/tico_config.h"
#include "overlay/translation_manager.h"
#include "tico/clocks.h"
#include "tico/dekopon_features.h"
#include "tico/game_overlay.h"
#include "tico/saves.h"
#include "tico/session_bridge.h"
#include "tico/tico_settings.h"
#include "video_core/gpu.h"
#include "video_core/overlay.h"
#include "video_core/renderer_vulkan/vk_lsfg.h"

namespace Common {
void StopAllThreadWorkers();
}

extern "C" {
u32 __nx_applet_type = AppletType_Application;
size_t __nx_heap_size = 0;
}

namespace {

namespace OverlayUI = SwitchFrontend::OverlayUI;
namespace GameOverlay = SwitchFrontend::GameOverlay;
namespace Saves = SwitchFrontend::Saves;

// Dekopon's own folder in tico's: config, NAND, logs, amiibo/ and camera/.
constexpr const char* kUserDir = "sdmc:/tico/system/dekopon/";
constexpr const char* kTicoLauncherPath = "sdmc:/switch/tico/tico.nro";

// Restart: the NRO starts itself again with the arguments it was given
bool relaunch = false;
std::vector<std::string> launch_args;

// the running game's title id, which names its states
std::atomic<u64> game_title_id{0};

PadState pad{};

bool FileExists(const std::string& path) {
    struct stat st {};
    return stat(path.c_str(), &st) == 0;
}

bool QueueTicoReturn() {
    UsbStorage::Shutdown(); // flush and unmount before tico takes over again
    SwitchFrontend::Clocks::Restore();
    if (!envHasNextLoad()) {
        return false;
    }
    if (relaunch && !launch_args.empty()) {
        std::string args;
        for (const std::string& arg : launch_args) {
            args += (args.empty() ? "\"" : " \"") + arg + "\"";
        }
        return envSetNextLoad(launch_args.front().c_str(), args.c_str()) == 0;
    }
    if (!FileExists(kTicoLauncherPath)) {
        return false;
    }
    char args[512];
    std::snprintf(args, sizeof(args), "%s --resume", kTicoLauncherPath);
    return envSetNextLoad(kTicoLauncherPath, args) == 0;
}

std::string GetRomPath(int argc, char** argv) {
    for (int i = 1; i < argc; i++) {
        if (argv[i] && std::strcmp(argv[i], "ticoSetup") != 0) {
            return argv[i];
        }
    }
    return {};
}

// tico passes the game's title after its path; the file's name otherwise.
std::string GetDisplayTitle(int argc, char** argv, const std::string& rom_path) {
    if (argc > 2 && argv[2] && argv[2][0] != '\0') {
        return argv[2];
    }
    std::string title = rom_path.substr(rom_path.find_last_of("/\\:") + 1);
    title = title.substr(0, title.find_last_of('.'));
    return title.empty() ? std::string{"Dekopon"} : title;
}

std::string TrFormat(const char* key, int value) {
    const std::string format = SwitchFrontend::OverlayTranslation::tr(key);
    char text[256];
    std::snprintf(text, sizeof(text), format.c_str(), value);
    return text;
}

void ShowToast(const std::string& message) {
    OverlayUI::ShowToast(message, OverlayUI::ToastCorner::TopRight);
}

// Frame generation reads its shaders from the user's own copy of Lossless.dll; says
// where it goes when the option is on and the file is not there, once per game.
void WarnIfLosslessMissing() {
    static bool warned = false;
    if (warned || !Settings::values.use_frame_generation.GetValue() ||
        Vulkan::IsLsfgShaderDllPresent()) {
        return;
    }
    warned = true;
    ShowToast(SwitchFrontend::OverlayTranslation::tr("settings_dekopon_frame_gen_missing_dll") +
              " " + Vulkan::GetLsfgShaderDllPath());
}

// A save state needs the guest's pending file and service work done, and the GPU idle, as
// Dekopon's own save path does.
bool PrepareForState(Core::System& system) {
    const auto start = std::chrono::steady_clock::now();
    while (system.KernelRunning() && system.Kernel().AreAsyncOperationsPending()) {
        if (std::chrono::steady_clock::now() - start > std::chrono::seconds(5) ||
            system.RunLoop() != Core::System::ResultStatus::Success) {
            return false;
        }
    }
    system.GPU().WaitIdle();
    return true;
}

bool SaveState(int slot) {
    const u64 title_id = game_title_id.load();
    if (title_id == 0) {
        return false;
    }
    std::string error;
    const bool ran = SwitchFrontend::RunOnEmuThreadAndWait([&](Core::System& system) {
        if (!PrepareForState(system)) {
            error = "busy";
            return;
        }
        try {
            Saves::SaveSlot(system, title_id, slot);
        } catch (const std::exception& e) {
            error = e.what();
        }
    });
    if (!ran || !error.empty()) {
        ShowToast(std::string{"Save failed: "} + (ran ? error : "the game stopped"));
        return false;
    }
    if (slot != OverlayUI::kAutoStateSlot) {
        ShowToast(TrFormat("emulator_state_saved", slot));
    }
    return true;
}

// What a Load State row loads: a slot, or one of the undo rows.
enum class StateLoad { Slot, UndoLoad, UndoSave };

void LoadState(StateLoad load, int slot) {
    const u64 title_id = game_title_id.load();
    std::string error;
    // the renderer may be made again: the overlay starts over after
    GameOverlay::Shutdown();
    const bool ran = SwitchFrontend::RunOnEmuThreadAndWait([&](Core::System& system) {
        if (!PrepareForState(system)) {
            error = "busy";
            return;
        }
        try {
            switch (load) {
            case StateLoad::Slot:
                Saves::LoadSlot(system, slot);
                break;
            case StateLoad::UndoLoad:
                Saves::UndoLoad(system);
                break;
            case StateLoad::UndoSave:
                Saves::UndoSave(system, title_id);
                break;
            }
            system.frame_limiter.AdvanceFrame();
        } catch (const std::exception& e) {
            error = e.what();
        }
    });
    GameOverlay::Init();
    if (!ran || !error.empty()) {
        ShowToast(std::string{"Load failed: "} + (ran ? error : "the game stopped"));
        return;
    }
    switch (load) {
    case StateLoad::Slot:
        ShowToast(slot == OverlayUI::kAutoStateSlot
                      ? SwitchFrontend::OverlayTranslation::tr("emulator_auto_loaded")
                      : TrFormat("emulator_state_loaded", slot));
        break;
    case StateLoad::UndoLoad:
        ShowToast(SwitchFrontend::OverlayTranslation::tr("emulator_undo_load_done"));
        break;
    case StateLoad::UndoSave:
        ShowToast(SwitchFrontend::OverlayTranslation::tr("emulator_undo_save_done"));
        break;
    }
}

// The session's state in the auto slot (Exit, Restart), what "Continue where
// you left off" loads at the next launch.
void WriteAutoSave() {
    static bool written = false;
    if (!written && SwitchFrontend::IsRunning()) {
        written = SaveState(OverlayUI::kAutoStateSlot);
    }
}

// Left by Restart: the relaunched game starts over instead of offering the auto save.
std::string RestartMarkerPath() {
    return Saves::StatePath(game_title_id.load(), OverlayUI::kAutoStateSlot) + ".restart";
}

// Continue where you left off (tico's General > Continue Last Game): the auto
// save, loaded or offered, unless the game was restarted.
void OfferAutoSave() {
    const bool restarted = std::remove(RestartMarkerPath().c_str()) == 0;
    // tico's General > Quick Menu Tip: shown once the game runs smoothly
    if (!restarted) {
        OverlayUI::ShowQuickMenuTip();
    }
    if (restarted || game_title_id.load() == 0 ||
        !FileExists(Saves::StatePath(game_title_id.load(), OverlayUI::kAutoStateSlot))) {
        return;
    }
    const std::string mode = SwitchFrontend::TicoConfig::ResumeOnLaunch();
    if (mode == "always") {
        LoadState(StateLoad::Slot, OverlayUI::kAutoStateSlot);
    } else if (mode != "never") {
        GameOverlay::ShowResumePrompt();
    }
}

// Closed from the HOME menu: the app is in the background, where the system doesn't run its
// GPU work, so the emulation can't be stopped (its shutdown waits on the GPU). The auto save
// gets a few seconds on a worker, the drives and clocks are given back, and the system, which
// waits on the exit lock, is let go on with closing the app.
[[noreturn]] void LeaveForSystemExit() {
    LOG_INFO(Frontend, "closed by the system: saving and leaving without stopping the emulation");
    static std::atomic<bool> done{false};
    // a save state needs a deep stack (its stream buffers alone are 128 KiB)
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setstacksize(&attr, 4 * 1024 * 1024);
    pthread_t worker;
    const bool started = pthread_create(
                             &worker, &attr,
                             [](void*) -> void* {
                                 WriteAutoSave();
                                 done.store(true);
                                 return nullptr;
                             },
                             nullptr) == 0;
    pthread_attr_destroy(&attr);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    while (started && !done.load() && std::chrono::steady_clock::now() < deadline) {
        svcSleepThread(10'000'000LL);
    }
    TicoBridge::PlaytimeFlush();
    UsbStorage::Shutdown();
    SwitchFrontend::Clocks::Restore();
    std::fflush(nullptr);
    // ending the process ourselves would look like a crash to the system:
    // unlocked, it ends the app itself, with every thread in it
    appletUnlockExit();
    while (true) {
        svcSleepThread(1'000'000'000LL);
    }
}

// A text setting (e.g. the console's name): the system keyboard, filled in with its value.
void EditTextOption() {
    const auto* option = OverlayUI::ConsumeTextEditOption();
    if (!option) {
        return;
    }
    SwkbdConfig keyboard;
    if (swkbdCreate(&keyboard, 0) != 0) {
        return;
    }
    const std::string current = SwitchFrontend::TicoConfig::GetOptionValue(*option);
    const std::string title = SwitchFrontend::OverlayTranslation::tr(option->label_key);
    swkbdConfigMakePresetDefault(&keyboard);
    swkbdConfigSetInitialText(&keyboard, current.c_str());
    swkbdConfigSetGuideText(&keyboard, title.c_str());
    if (option->max_length > 0) {
        swkbdConfigSetStringLenMax(&keyboard, static_cast<u32>(option->max_length));
    }
    char typed[512] = {};
    if (swkbdShow(&keyboard, typed, sizeof(typed)) == 0) {
        SwitchFrontend::TicoConfig::SetOptionValue(*option, typed);
        OverlayUI::NotifyOptionEdited(*option);
    }
    swkbdClose(&keyboard);
}

// Settings > Players: the 3DS has one player, on the handheld or the first controller.
std::vector<std::string> DescribePlayers() {
    const auto name = [](u32 style) -> std::string {
        const char* key = "emulator_pad_other";
        if (style & HidNpadStyleTag_NpadFullKey) {
            key = "emulator_pad_pro";
        } else if (style & HidNpadStyleTag_NpadHandheld) {
            key = "emulator_pad_handheld";
        } else if (style & HidNpadStyleTag_NpadJoyDual) {
            key = "emulator_pad_joycon_pair";
        } else if (style & HidNpadStyleTag_NpadJoyLeft) {
            key = "emulator_pad_joycon_left";
        } else if (style & HidNpadStyleTag_NpadJoyRight) {
            key = "emulator_pad_joycon_right";
        } else if (style & HidNpadStyleTag_NpadGc) {
            key = "emulator_pad_gamecube";
        }
        return SwitchFrontend::OverlayTranslation::tr(key);
    };
    u32 style = hidGetNpadStyleSet(HidNpadIdType_No1);
    if (!style) {
        style = hidGetNpadStyleSet(HidNpadIdType_Handheld);
    }
    return {style ? name(style) : std::string{}};
}

// The system's controller screen, to choose the controller that plays.
bool ShowControllerOrder() {
    HidLaControllerSupportArg arg;
    hidLaCreateControllerSupportArg(&arg);
    arg.hdr.player_count_min = 0;
    arg.hdr.player_count_max = 1;
    HidLaControllerSupportResultInfo info{};
    return hidLaShowControllerSupport(&info, &arg) == 0;
}

void ConfigureOverlay(const std::string& display_title) {
    OverlayUI::SetGameTitle(display_title);
    OverlayUI::SetSlotOccupiedCallback([](int slot) {
        const u64 title_id = game_title_id.load();
        return title_id != 0 && slot > 0 && FileExists(Saves::StatePath(title_id, slot));
    });
    // Called while the menu is drawn: each slot's picture, taken when it was saved.
    OverlayUI::SetSlotPreviewCallback([](int slot) {
        static std::array<unsigned long long, OverlayUI::kAutoStateSlot + 1> pictures{};
        OverlayUI::SlotPreview preview;
        const u64 title_id = game_title_id.load();
        if (title_id == 0 || slot < 1 || slot >= static_cast<int>(pictures.size())) {
            return preview;
        }
        // the slot may have been saved again since
        GameOverlay::FreePicture(pictures[slot]);
        pictures[slot] = 0;

        struct stat st {};
        if (stat(Saves::StatePath(title_id, slot).c_str(), &st) != 0) {
            return preview;
        }
        char when[32];
        if (std::strftime(when, sizeof(when), "%Y-%m-%d %H:%M", std::localtime(&st.st_mtime))) {
            preview.saved_at = when;
        }
        pictures[slot] =
            GameOverlay::LoadPicture(Saves::PicturePath(title_id, slot), &preview.aspect);
        preview.texture = pictures[slot];
        return preview;
    });
    OverlayUI::PlayerCallbacks players;
    players.ports = &DescribePlayers;
    OverlayUI::SetPlayerCallbacks(std::move(players));
    OverlayUI::SetUndoStateCallback([] {
        OverlayUI::UndoStateInfo info;
        info.can_undo_load = Saves::CanUndoLoad();
        info.overwritten_saved_at = Saves::OverwrittenSavedAt();
        return info;
    });
}

// Returns true when the game should close (Exit, Restart).
bool HandleOverlayAction(OverlayUI::Action action) {
    using OverlayUI::Action;
    if (OverlayUI::IsSaveStateAction(action)) {
        GameOverlay::SetVisible(false);
        SaveState(OverlayUI::GetStateSlotForAction(action));
        return false;
    }
    if (OverlayUI::IsLoadStateAction(action) || action == Action::UndoLoadState ||
        action == Action::UndoSaveState) {
        const StateLoad load = action == Action::UndoLoadState   ? StateLoad::UndoLoad
                               : action == Action::UndoSaveState ? StateLoad::UndoSave
                                                                 : StateLoad::Slot;
        GameOverlay::SetVisible(false);
        LoadState(load, OverlayUI::GetStateSlotForAction(action));
        return false;
    }

    switch (action) {
    case Action::Exit:
        WriteAutoSave();
        GameOverlay::SetVisible(false);
        return true;
    // the game from the start: this NRO again, with the same game
    case Action::Restart:
    case Action::Reset:
        WriteAutoSave();
        if (std::FILE* marker = std::fopen(RestartMarkerPath().c_str(), "wb")) {
            std::fclose(marker);
        }
        GameOverlay::SetVisible(false);
        relaunch = true;
        return true;
    case Action::EditText:
        EditTextOption();
        break;
    case Action::ControllerOrder:
        if (!ShowControllerOrder()) {
            ShowToast(SwitchFrontend::OverlayTranslation::tr("emulator_controllers_failed"));
        }
        break;
    case Action::NoticeChoice:
        OverlayUI::ConsumeNoticeChoice();
        GameOverlay::SetVisible(false);
        break;
    default:
        break;
    }
    return false;
}

// Dekopon's frontend reports some things as its own on-screen toasts: they go to tico's.
void ForwardDekoponToast() {
    const std::string toast = VideoCore::GetOverlayToast();
    if (!toast.empty()) {
        VideoCore::PostOverlayToast({}, 0);
        ShowToast(toast);
    }
}

// The buttons Dekopon binds to its in-game actions (Controls > Buttons).
void HandleHotkeys(u64 input_pressed) {
    using SwitchFrontend::ButtonMask;
    using SwitchFrontend::GetMapping;
    using SwitchFrontend::MappableControl;
    if ((input_pressed & ButtonMask(GetMapping(MappableControl::CycleLayout))) != 0) {
        SwitchFrontend::CycleScreenLayout();
        ShowToast(SwitchFrontend::CurrentScreenLayoutName());
    }
    if ((input_pressed & ButtonMask(GetMapping(MappableControl::TogglePointer))) != 0) {
        SwitchFrontend::TogglePointerMode();
    }
    if ((input_pressed & ButtonMask(GetMapping(MappableControl::SwapScreens))) != 0) {
        SwitchFrontend::ToggleSwapScreens();
    }
}

// Runs the game until it closes; true when it ran.
bool RunGame(const std::string& rom_path, const std::string& display_title) {
    if (!SwitchFrontend::CreateWindow(nwindowGetDefault())) {
        LOG_CRITICAL(Frontend, "could not create the render window");
        return false;
    }
    SwitchFrontend::ResetPointer();
    if (!SwitchFrontend::BootRom(rom_path)) {
        SwitchFrontend::DestroyWindow();
        return false;
    }

    ConfigureOverlay(display_title);
    // first on the emulation thread, once the game is loaded and before it runs
    SwitchFrontend::RunOnEmuThread([](Core::System& system) {
        u64 title_id = 0;
        system.GetAppLoader().ReadProgramId(title_id);
        game_title_id.store(title_id);
        SwitchFrontend::DekoponFeatures::Register(system, title_id);
        SwitchFrontend::TicoSettings::ApplyLoaded(system);
    });
    GameOverlay::Init();

    using Clock = std::chrono::steady_clock;
    bool auto_save_offered = false;
    bool was_visible = false;
    auto picture_deadline = Clock::now();
    u8 operation_mode = appletGetOperationMode();
    u64 prev_input = 0;
    while (true) {
        if (!appletMainLoop()) {
            LeaveForSystemExit();
        }
        if (!SwitchFrontend::IsRunning()) {
            break;
        }
        // Blocks while the guest's keyboard is up
        SwitchFrontend::PumpKeyboard();
        SwitchFrontend::UpdateDisplayMode();
        SwitchFrontend::Clocks::Keep();
        // docking or undocking can change the resolution (Docked Resolution)
        if (const u8 mode = appletGetOperationMode(); mode != operation_mode) {
            operation_mode = mode;
            SwitchFrontend::TicoSettings::ApplyLive();
        }

        if (!auto_save_offered && game_title_id.load() != 0) {
            auto_save_offered = true;
            WarnIfLosslessMissing();
            OfferAutoSave();
        }

        SwitchFrontend::InputState state;
        const u64 held = SwitchFrontend::PollInput(pad, state);
        GameOverlay::Update(&pad);
        const bool visible = GameOverlay::IsVisible();

        // While the menu is up, or Plus and Minus open it, the guest sees neutral input.
        constexpr u64 kMenuChord = HidNpadButton_Plus | HidNpadButton_Minus;
        const bool chord = (held & kMenuChord) == kMenuChord;
        SwitchFrontend::UpdateInput(visible || chord ? SwitchFrontend::InputState{} : state);
        if (!visible && !chord) {
            HandleHotkeys(state.buttons & ~prev_input);
        }
        prev_input = state.buttons;

        // The game pauses once the picture of it (saved with the states made from
        // the menu) is taken, a frame or two after the menu opens.
        if (visible && !was_visible) {
            SwitchFrontend::RunOnEmuThread([](Core::System& system) {
                Saves::RequestPicture(system);
            });
            picture_deadline = Clock::now() + std::chrono::milliseconds(200);
        }
        was_visible = visible;
        const bool taking_picture =
            visible && !Saves::PictureDone() && Clock::now() < picture_deadline;
        SwitchFrontend::SetEmulationPaused(visible && !taking_picture);
        if (!visible) {
            TicoBridge::PlaytimeFrame(); // the game ran: to the player's playtime
        }

        if (OverlayUI::ConsumeSettingsChanged()) {
            SwitchFrontend::TicoSettings::ApplyLive();
            WarnIfLosslessMissing();
        }
        if (HandleOverlayAction(GameOverlay::ConsumeAction())) {
            break;
        }

        ForwardDekoponToast();
        SwitchFrontend::UpdateRealAmiibo();

        // The async GPU thread shares core 0 with this loop.
        svcSleepThread(4'000'000);
    }

    TicoBridge::PlaytimeFlush(); // the game's last seconds
    SwitchFrontend::EndRealAmiibo();
    GameOverlay::Shutdown();
    SwitchFrontend::StopRom();
    SwitchFrontend::DestroyWindow();
    return true;
}

int Run(int argc, char** argv) {
    appletLockExit();
    for (int i = 0; i < argc; i++) {
        launch_args.emplace_back(argv[i] ? argv[i] : "");
    }
    // drives mount in the background; a game on one comes as usb://<volume>/<path>
    UsbStorage::Init();

    const std::string rom_arg = GetRomPath(argc, argv);
    const std::string rom_path = rom_arg.empty() ? std::string{} : UsbStorage::Resolve(rom_arg);
    if (rom_path.empty()) {
        // started without a game, or its USB drive is not connected: back to tico
        const bool queued = QueueTicoReturn();
        appletUnlockExit();
        return queued ? EXIT_SUCCESS : EXIT_FAILURE;
    }

    // the overlay's fonts, strings, pictures and settings.json
    const bool have_romfs = romfsInit() == 0;
    Common::Horizon::PinCurrentThread(Common::Horizon::CoreFrontend);

    SwitchFrontend::Bootstrap(kUserDir);
    // tico's settings, and this game's own when it has them (Settings > This Game)
    SwitchFrontend::TicoConfig::ReloadConfig();
    SwitchFrontend::TicoConfig::SetGame(rom_path);
    SwitchFrontend::TicoSettings::Apply();
    // the SD card's saves and the states in tico's folders
    Saves::UseTicoFolders();
    if (SwitchFrontend::TicoSettings::BoostMode()) {
        SwitchFrontend::Clocks::Boost();
    }

    padConfigureInput(1, HidNpadStyleSet_NpadStandard);
    padInitializeDefault(&pad);
    hidInitializeTouchScreen();
    SwitchFrontend::StartSixAxis();
    SwitchFrontend::InitializeInput();

    RunGame(rom_path, GetDisplayTitle(argc, argv, rom_path));

    SwitchFrontend::ShutdownInput();
    SwitchFrontend::StopSixAxis();
    SwitchFrontend::Shutdown();
    Common::StopAllThreadWorkers();
    if (have_romfs) {
        romfsExit();
    }
    const bool queued = QueueTicoReturn();
    appletUnlockExit();
    return queued ? EXIT_SUCCESS : EXIT_FAILURE;
}

} // namespace

int main(int argc, char** argv) {
    return Run(argc, argv);
}
