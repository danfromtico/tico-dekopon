// Copyright 2026 Dan | ticoverse.com
// SPDX-License-Identifier: GPL-2.0-or-later

// The renderer tico's overlay draws with: Dekopon's Vulkan presentation (GameOverlay).

#include "overlay/overlay_renderer.h"

#include "tico/game_overlay.h"

namespace SwitchFrontend::OverlayRenderer {

bool Init() {
    return GameOverlay::RendererInit();
}

void Shutdown() {
    GameOverlay::RendererShutdown();
}

void BeginFrame() {
    GameOverlay::RendererBeginFrame();
}

} // namespace SwitchFrontend::OverlayRenderer
