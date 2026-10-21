// Copyright 2026 Azahar Emulator Project
// Copyright 2026 Dan | ticoverse.com
// SPDX-License-Identifier: GPL-2.0-or-later

#include "tico/tico_settings.h"

#include <algorithm>
#include <map>
#include <optional>
#include <string>
#include <string_view>

#include "audio_core/dsp_interface.h"
#include "citra_switch/config.h"
#include "citra_switch/input.h"
#include "citra_switch/settings_registry.h"
#include "common/logging/log.h"
#include "common/settings.h"
#include "common/string_util.h"
#include "core/core.h"
#include "core/core_timing.h"
#include "core/hle/service/cfg/cfg.h"
#include "overlay/tico_config.h"
#include "tico/game_overlay.h"
#include "tico/switch_libnx.h"

namespace SwitchFrontend::TicoSettings {
namespace {

namespace TicoConfig = SwitchFrontend::TicoConfig;

constexpr std::string_view kPrefix = "dekopon_";

// The frontend's own options; every other dekopon_* key is a registry setting.
constexpr std::string_view kScreenLayout = "dekopon_screen_layout";
constexpr std::string_view kSwapScreens = "dekopon_swap_screens";
constexpr std::string_view kResolution = "dekopon_resolution_factor";
constexpr std::string_view kDockedResolution = "dekopon_docked_resolution";
constexpr std::string_view kBoostMode = "dekopon_boost_mode";
constexpr std::string_view kShaderNotice = "dekopon_shader_notice";
constexpr std::string_view kUsername = "dekopon_username";
constexpr std::string_view kLanguage = "dekopon_language";
constexpr std::string_view kMappingPrefix = "dekopon_map_";

using Values = std::map<std::string, std::string, std::less<>>;

// Every dekopon_* option as tico stores it, settings.json's default where nothing is set.
Values CurrentValues() {
    Values values;
    TicoConfig::ApplyToCore([&values](const std::string& key, const std::string& value) {
        values[key] = value;
    });
    return values;
}

std::string Get(const Values& values, std::string_view key) {
    const auto it = values.find(key);
    return it != values.end() ? it->second : std::string{};
}

std::optional<int> GetInt(const Values& values, std::string_view key) {
    const std::string value = Get(values, key);
    if (value.empty()) {
        return std::nullopt;
    }
    try {
        return std::stoi(value);
    } catch (...) {
        return std::nullopt;
    }
}

bool NeedsRestart(std::string_view key) {
    const TicoConfig::OptionDef* option = TicoConfig::FindOption(key);
    return option && option->needs_restart;
}

// What the last Apply or ApplyLive set, to apply only what changed since.
Values s_applied;

// docked, Docked Resolution when it is set, Internal Resolution otherwise
std::string ResolutionValue(const Values& values) {
    const std::string docked = Get(values, kDockedResolution);
    if (appletGetOperationMode() == AppletOperationMode_Console && !docked.empty() &&
        docked != "same") {
        return docked;
    }
    return Get(values, kResolution);
}

// A registry setting, read as Dekopon reads its config.ini.
bool LoadEntry(std::string_view key, const std::string& value) {
    const SettingEntry* entry = FindEntry(key.substr(kPrefix.size()));
    if (!entry || !entry->load) {
        return false;
    }
    entry->load(value);
    return true;
}

// Controls > Buttons: each control on the Switch button whose index the value holds.
bool LoadMapping(std::string_view key, const std::string& value) {
    const std::string_view name = key.substr(kPrefix.size());
    for (int i = 0; i < NumMappableControls; ++i) {
        const auto control = static_cast<MappableControl>(i);
        if (name != ControlConfigKey(control)) {
            continue;
        }
        int button = static_cast<int>(DefaultMapping(control));
        try {
            button = std::stoi(value);
        } catch (...) {
        }
        SetMapping(control, static_cast<InputButton>(std::clamp(button, 0, NumBindingChoices - 1)));
        return true;
    }
    return false;
}

void ApplySwapScreens(bool swapped) {
    if (Settings::values.swap_screen.GetValue() != swapped) {
        ToggleSwapScreens();
    }
}

// Applies `values`, all of them or only what differs from `s_applied`; returns whether a button
// mapping changed.
bool ApplyValues(const Values& values, bool live) {
    bool mappings_changed = false;
    for (const auto& [key, value] : values) {
        if (live) {
            const auto it = s_applied.find(key);
            if ((it != s_applied.end() && it->second == value) || NeedsRestart(key)) {
                continue;
            }
        }
        if (key == kScreenLayout || key == kSwapScreens || key == kResolution ||
            key == kDockedResolution || key == kBoostMode || key == kUsername ||
            key == kLanguage) {
            continue;
        }
        if (key == kShaderNotice) {
            GameOverlay::SetShaderNotice(value == "true");
        } else if (key.starts_with(kMappingPrefix) && LoadMapping(key, value)) {
            mappings_changed = true;
        } else if (!LoadEntry(key, value)) {
            LOG_WARNING(Frontend, "tico settings: {} is not one of Dekopon's settings", key);
        }
    }

    const std::string resolution = ResolutionValue(values);
    if (!resolution.empty()) {
        LoadEntry(kResolution, resolution);
    }
    // tico's HUD draws the FPS counter and the shader notice
    Settings::values.show_fps = false;
    Settings::values.show_shader_compile_notice = false;
    if (mappings_changed) {
        ApplyButtonMappings();
    }
    s_applied = values;
    return mappings_changed;
}

} // namespace

void Apply() {
    ApplyValues(CurrentValues(), false);
}

void ApplyLive() {
    const Values values = CurrentValues();
    const Values before = s_applied;
    ApplyValues(values, true);

    if (Get(values, kScreenLayout) != Get(before, kScreenLayout)) {
        if (const auto layout = GetInt(values, kScreenLayout)) {
            SetScreenLayout(*layout);
        }
        ApplySwapScreens(Get(values, kSwapScreens) == "true");
    } else if (Get(values, kSwapScreens) != Get(before, kSwapScreens)) {
        ApplySwapScreens(Get(values, kSwapScreens) == "true");
    }
    RequestLayoutUpdate();

    // what Dekopon's own menu does as these change
    const u32 clock = static_cast<u32>(Settings::values.cpu_clock_percentage.GetValue());
    const bool stretching = Settings::values.enable_audio_stretching.GetValue();
    RunOnEmuThread([clock, stretching](Core::System& system) {
        if (!system.IsPoweredOn()) {
            return;
        }
        system.CoreTiming().UpdateClockSpeed(clock);
        system.DSP().EnableStretching(stretching);
    });
}

void ApplyLoaded(Core::System& system) {
    const Values values = CurrentValues();
    if (const auto layout = GetInt(values, kScreenLayout)) {
        SetScreenLayout(*layout);
    }
    ApplySwapScreens(Get(values, kSwapScreens) == "true");

    auto cfg = Service::CFG::GetModule(system);
    if (!cfg) {
        return;
    }
    if (const auto language = GetInt(values, kLanguage)) {
        cfg->SetSystemLanguage(static_cast<Service::CFG::SystemLanguage>(*language));
    }
    std::u16string username = Common::UTF8ToUTF16(Get(values, kUsername));
    if (!username.empty()) {
        // the console's name holds 10 characters
        username.resize(std::min<std::size_t>(username.size(), 10));
        cfg->SetUsername(username);
    }
    const Result rc = cfg->UpdateConfigNANDSavegame();
    LOG_INFO(Frontend, "tico settings: console language and name written (rc=0x{:x})", rc.raw);
}

bool BoostMode() {
    return TicoConfig::GetConfigValue(kBoostMode, "false") == "true";
}

} // namespace SwitchFrontend::TicoSettings
