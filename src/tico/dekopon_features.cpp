// Copyright 2026 Dan | ticoverse.com
// SPDX-License-Identifier: GPL-2.0-or-later

#include "tico/dekopon_features.h"

#include "tico/switch_libnx.h"

#include <cstdio>
#include <mutex>
#include <string>
#include <vector>

#include "citra_switch/amiibo_session.h"
#include "citra_switch/camera/still_image_camera.h"
#include "citra_switch/config.h"
#include "citra_switch/menu_data.h"
#include "core/cheats/cheat_base.h"
#include "core/cheats/cheats.h"
#include "core/core.h"
#include "core/hle/kernel/kernel.h"
#include "core/hle/service/cam/cam.h"
#include "core/hle/service/nfc/nfc.h"
#include "core/hle/service/sm/sm.h"
#include "overlay/overlay_ui.h"
#include "overlay/translation_manager.h"

namespace SwitchFrontend::DekoponFeatures {
namespace {

namespace OverlayUI = SwitchFrontend::OverlayUI;
using OverlayUI::ModMenuEntry;

// What each row of the Amiibo & Camera list does, by its source index.
constexpr int kAmiiboFileBase = 0;
constexpr int kRemoveAmiibo = 10000;
constexpr int kScanRealAmiibo = 10001;
constexpr int kOverwriteAmiibo = 10002;
constexpr int kCameraTarget = 20000;
constexpr int kClearCamera = 20001;
constexpr int kCameraImageBase = 30000;

// The files offered, read again each time the list opens.
std::mutex s_files_mutex;
std::vector<FileEntry> s_amiibos;
std::vector<FileEntry> s_camera_images;
// The overwrite row asks to be chosen twice.
bool s_overwrite_armed = false;

std::string Tr(const char* key, const char* fallback) {
    const std::string text = OverlayTranslation::tr(key);
    return text.empty() || text == key ? fallback : text;
}

void Toast(const std::string& message) {
    OverlayUI::ShowToast(message, OverlayUI::ToastCorner::TopRight);
}

std::shared_ptr<Service::NFC::Module::Interface> Nfc() {
    auto& system = Core::System::GetInstance();
    if (!system.IsPoweredOn()) {
        return nullptr;
    }
    return system.ServiceManager().GetService<Service::NFC::Module::Interface>("nfc:u");
}

void LoadAmiibo(const std::string& path) {
    auto nfc = Nfc();
    if (!nfc) {
        Toast(Tr("dekopon_amiibo_no_nfc", "The NFC service is unavailable"));
        return;
    }
    std::scoped_lock lock{Core::System::GetInstance().Kernel().GetHLELock()};
    if (nfc->IsTagActive()) {
        Toast(Tr("dekopon_amiibo_already_active", "An Amiibo is already active"));
    } else if (!nfc->IsSearchingForAmiibos()) {
        Toast(Tr("dekopon_amiibo_not_scanning", "The game is not scanning for Amiibo"));
    } else if (!nfc->LoadAmiibo(path)) {
        Toast(Tr("dekopon_amiibo_load_failed", "Could not load the Amiibo file"));
    } else {
        Toast(Tr("dekopon_amiibo_loaded", "Amiibo loaded"));
    }
}

void RemoveAmiibo() {
    auto nfc = Nfc();
    if (!nfc) {
        Toast(Tr("dekopon_amiibo_no_nfc", "The NFC service is unavailable"));
        return;
    }
    std::scoped_lock lock{Core::System::GetInstance().Kernel().GetHLELock()};
    if (!nfc->IsTagActive()) {
        Toast(Tr("dekopon_amiibo_none_active", "No Amiibo is active"));
        return;
    }
    nfc->RemoveAmiibo();
    Toast(Tr("dekopon_amiibo_removed", "Amiibo removed"));
}

// Makes a running game pick the cameras up again.
void ReloadGuestCameras() {
    auto& system = Core::System::GetInstance();
    if (!system.IsPoweredOn()) {
        return;
    }
    if (auto cam = Service::CAM::GetModule(system)) {
        cam->ReloadCameraDevices();
    }
}

void SelectCameraImage(const std::string& path) {
    // decoded here, so a broken file is reported rather than shown as nothing
    const std::string error = Camera::StillImage::Preload(path);
    if (!error.empty()) {
        Toast(Tr("dekopon_camera_bad_image", "Cannot use that image") + std::string{": "} +
              error);
        return;
    }
    SetCameraImage(path, GetCameraTarget());
    ReloadGuestCameras();
    SaveConfig();
    Toast(Tr("dekopon_camera_set", "Cameras now show this image"));
}

void ClearCameraImage() {
    SetCameraImage("", GetCameraTarget());
    ReloadGuestCameras();
    SaveConfig();
    Toast(Tr("dekopon_camera_cleared", "Cameras cleared"));
}

std::vector<ModMenuEntry> ListRows() {
    std::scoped_lock lock{s_files_mutex};
    s_amiibos = ListAmiiboFiles();
    s_camera_images = ListCameraImages();

    std::vector<ModMenuEntry> rows;
    rows.push_back({Tr("dekopon_amiibo", "Amiibo"), {}, -1});
    for (std::size_t i = 0; i < s_amiibos.size(); ++i) {
        rows.push_back({s_amiibos[i].name, Tr("dekopon_amiibo_load", "Load"),
                        kAmiiboFileBase + static_cast<int>(i)});
    }
    if (s_amiibos.empty()) {
        rows.push_back({Tr("dekopon_amiibo_empty", "No .bin files in amiibo/"), {}, -1});
    }
    std::string scan_value;
    if (IsRealAmiiboActive()) {
        scan_value = Tr("dekopon_amiibo_on_reader", "On the reader");
    } else if (IsRealAmiiboScanning()) {
        scan_value = Tr("dekopon_amiibo_waiting", "Waiting...");
    }
    rows.push_back({Tr("dekopon_amiibo_scan", "Scan a real Amiibo"), scan_value, kScanRealAmiibo});
    if (HasPendingAmiiboOverwrite()) {
        rows.push_back({Tr("dekopon_amiibo_overwrite", "Save anyway, erasing") + std::string{" "} +
                            PendingAmiiboOverwriteOwner(),
                        s_overwrite_armed ? Tr("dekopon_press_again", "Press again") : "",
                        kOverwriteAmiibo});
    }
    rows.push_back({Tr("dekopon_amiibo_remove", "Remove active Amiibo"), {}, kRemoveAmiibo});

    rows.push_back({Tr("dekopon_camera", "Camera"), {}, -1});
    rows.push_back({Tr("dekopon_camera_target", "Feed image to"),
                    CameraTargetName(GetCameraTarget()), kCameraTarget});
    for (std::size_t i = 0; i < s_camera_images.size(); ++i) {
        rows.push_back({s_camera_images[i].name,
                        s_camera_images[i].path == GetCameraImage()
                            ? Tr("dekopon_camera_in_use", "In use")
                            : std::string{},
                        kCameraImageBase + static_cast<int>(i)});
    }
    if (s_camera_images.empty()) {
        rows.push_back(
            {Tr("dekopon_camera_empty", "No .png/.jpg files in camera/"), {}, -1});
    }
    rows.push_back({Tr("dekopon_camera_clear", "Show nothing"), {}, kClearCamera});
    return rows;
}

bool StepRow(int source_index, int direction) {
    if (source_index != kOverwriteAmiibo) {
        s_overwrite_armed = false;
    }
    if (source_index == kCameraTarget) {
        const int count = NumCameraTargets;
        const int next = (static_cast<int>(GetCameraTarget()) + direction + count) % count;
        SetCameraImage(GetCameraImage(), static_cast<CameraTarget>(next));
        ReloadGuestCameras();
        SaveConfig();
        return true;
    }
    // everything else is an action, run once per press
    if (direction < 0) {
        return false;
    }
    if (source_index == kRemoveAmiibo) {
        RemoveAmiibo();
    } else if (source_index == kScanRealAmiibo) {
        if (IsRealAmiiboScanning()) {
            CancelRealAmiiboScan();
        } else {
            std::string message;
            BeginRealAmiiboScan(message);
            if (!message.empty()) {
                Toast(message);
            }
        }
    } else if (source_index == kOverwriteAmiibo) {
        if (!s_overwrite_armed) {
            s_overwrite_armed = true;
        } else {
            s_overwrite_armed = false;
            std::string message;
            ConfirmAmiiboOverwrite(message);
            if (!message.empty()) {
                Toast(message);
            }
        }
    } else if (source_index == kClearCamera) {
        ClearCameraImage();
    } else {
        std::string path;
        bool amiibo = false;
        {
            std::scoped_lock lock{s_files_mutex};
            if (source_index >= kCameraImageBase) {
                const auto i = static_cast<std::size_t>(source_index - kCameraImageBase);
                if (i < s_camera_images.size()) {
                    path = s_camera_images[i].path;
                }
            } else if (source_index >= kAmiiboFileBase) {
                const auto i = static_cast<std::size_t>(source_index - kAmiiboFileBase);
                if (i < s_amiibos.size()) {
                    path = s_amiibos[i].path;
                    amiibo = true;
                }
            }
        }
        if (path.empty()) {
            return false;
        }
        if (amiibo) {
            LoadAmiibo(path);
        } else {
            SelectCameraImage(path);
        }
    }
    return true;
}

} // namespace

void Register(Core::System& system, unsigned long long title_id) {
    OverlayUI::SetPortalCallbacks(&ListRows, &StepRow);

    OverlayUI::SetCheatCallbacks(
        [&system]() {
            std::vector<OverlayUI::CheatMenuEntry> entries;
            const auto cheats = system.CheatEngine().GetCheats();
            entries.reserve(cheats.size());
            for (std::size_t i = 0; i < cheats.size(); ++i) {
                const auto& cheat = cheats[i];
                if (!cheat) {
                    continue;
                }
                std::string name = cheat->GetName();
                if (name.empty()) {
                    char fallback[32];
                    std::snprintf(fallback, sizeof(fallback), "Cheat %zu", i + 1);
                    name = fallback;
                }
                entries.push_back({std::move(name), cheat->IsEnabled(), true,
                                   static_cast<int>(i)});
            }
            return entries;
        },
        [&system, title_id](int source_index) {
            if (title_id == 0 || source_index < 0) {
                return false;
            }
            const auto cheats = system.CheatEngine().GetCheats();
            const auto index = static_cast<std::size_t>(source_index);
            if (index >= cheats.size() || !cheats[index]) {
                return false;
            }
            cheats[index]->SetEnabled(!cheats[index]->IsEnabled());
            SaveCheats(system, title_id);
            return true;
        });
}

void SaveCheats(Core::System& system, unsigned long long title_id) {
    if (title_id != 0) {
        system.CheatEngine().SaveCheatFile(title_id);
    }
}

} // namespace SwitchFrontend::DekoponFeatures
