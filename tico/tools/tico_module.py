#!/usr/bin/env python3
"""Settings definition and strings for the tico module of Dekopon.

tico/module/settings.json lists every setting tico's settings screen and the
quick menu show, laid out in tabs as Dekopon's own Settings are (General,
Graphics, Enhancements, 3D, Audio, Layout, Controls, System, Advanced).
Labels are translation keys; the strings go into tico/lang/*.json
(settings_dekopon_*), next to the overlay's own strings.

A key is dekopon_ and the setting's config.ini key in Dekopon's registry
(src/citra_switch/settings_registry.cpp), and its values are spelled as
config.ini spells them, so src/tico/tico_settings.cpp hands them to the
registry as they are. A few are the frontend's own (tico_settings.cpp says
which). Defaults are Dekopon's.

English is Dekopon's wording; the other languages come from
tico/tools/translations.json where it has the same English text, and are
English otherwise.

    python3 tico/tools/tico_module.py
"""

from __future__ import annotations

import json
import re
from collections import OrderedDict
from pathlib import Path

TICO = Path(__file__).resolve().parents[1]
SETTINGS = TICO / "module/settings.json"
LANG_DIR = TICO / "lang"
TRANSLATIONS = json.loads((TICO / "tools/translations.json").read_text(encoding="utf-8"))
LANGUAGES = ("en", "de", "es", "fr", "ja", "pt", "ru", "zh")
P = "settings_dekopon_"
# the strings an earlier module wrote, replaced by these
OLD_PREFIXES = ("settings_azahar_",)


def choices(*pairs):
    return [OrderedDict(label=label, value=value) for label, value in pairs]


def indexed(*labels, start=0):
    """Choices stored as their index, as config.ini stores an enum."""
    return choices(*((label, str(i)) for i, label in enumerate(labels, start)))


def percent(values):
    return choices(*((f"{v}%", str(v)) for v in values))


BOOL = "bool"
ENUM = "enum"
TEXT = "string"

# label text of every option, section and tab, by its key's slug
LABELS: dict[str, str] = {}


def option(key, label, kind, default, restart=False, *, choices=None, prefix=True, **extra):
    slug = re.sub(r"[^a-z0-9]+", "_", key.lower()).strip("_")
    LABELS[slug] = label
    entry = OrderedDict(key=("dekopon_" + key) if prefix else key, label=P + slug, type=kind,
                        default=default)
    if restart:
        entry["restart"] = True
    if choices is not None:
        entry["choices"] = choices
    entry.update(extra)
    return entry


def when(key, value):
    return OrderedDict(key="dekopon_" + key, value=value)


def section(slug, label, *options):
    LABELS["section_" + slug] = label
    return ("section_" + slug, list(options))


def tab(slug, label, *sections):
    LABELS["tab_" + slug] = label
    return ("tab_" + slug, list(sections))


# Dekopon's lists, in the order its enums have them
TEXTURE_FILTERS = indexed("None", "Anime4K", "Bicubic", "ScaleForce", "xBRZ", "MMPX")
TEXTURE_SAMPLING = indexed("Game Controlled", "Nearest Neighbor", "Linear")
ANISOTROPY = indexed("Off", "2x", "4x", "8x", "16x")
AUDIO_EMULATION = indexed("HLE", "LLE", "LLE (multithreaded)")
STEREO = indexed("Off", "Side by Side", "Side by side (full)", "Anaglyph", "Interlaced",
                 "Reverse interlaced", "Nintendo Labo VR")
MONO_EYE = indexed("Left eye", "Right eye")
SMALL_SCREEN_POSITIONS = indexed("Top right", "Middle right", "Bottom right", "Top left",
                                 "Middle left", "Bottom left", "Top centre", "Bottom centre")
REGIONS = indexed("Auto", "Japan", "USA", "Europe", "Australia", "China", "Korea", "Taiwan",
                  start=-1)
LANGUAGES_3DS = indexed("Japanese", "English", "French", "German", "Italian", "Spanish",
                        "Simplified Chinese", "Korean", "Dutch", "Portuguese", "Russian",
                        "Traditional Chinese")
INIT_CLOCK = indexed("System time", "Fixed time")
# Dekopon's screen layout presets (src/citra_switch/emulation.cpp)
SCREEN_LAYOUTS = indexed("Vertical stack", "Side by Side", "Large top, small bottom",
                         "Large bottom, small top", "Top screen only", "Bottom screen only",
                         "Vertical stack (rotate console)", "Hybrid",
                         "Vertical stack (rotate console other way)", "Bottom screen overlay")
RESOLUTIONS = choices(("Auto (window)", "0"), ("Native", "1"), ("2x", "2"), ("3x", "3"),
                      ("4x", "4"))
HUD_POSITIONS = choices(("Hidden", "hidden"), ("Top left", "top_left"), ("Top right", "top_right"),
                        ("Bottom left", "bottom_left"), ("Bottom right", "bottom_right"))
# the Switch buttons a control can be on, by Dekopon's InputButton index
SWITCH_BUTTONS = indexed("A", "B", "X", "Y", "Up", "Down", "Left", "Right", "L", "R", "Plus",
                         "Minus", "ZL", "ZR", "Left stick press", "Right stick press",
                         "Disabled")
POINTER_SOURCES = indexed("Left stick", "Gyro", "Right stick")
GYRO_SOURCES = indexed("Automatic", "Left controller", "Right controller", "Console")
GYRO_ORIENTATIONS = indexed("Horizontal", "Vertical")


def button(key, label, default):
    return option("map_" + key, label, ENUM, str(default), choices=SWITCH_BUTTONS)


FRAME_GEN = when("use_frame_generation", "true")
OVERLAY_LAYOUT = when("screen_layout", "9")
MOVIE_THROTTLE = when("movie_cpu_throttle", "true")

TABS = [
    tab("general", "General",
        section("performance", "Performance",
                option("frame_limit", "Frame Limit", ENUM, "100",
                       choices=choices(("Unlimited", "0")) +
                       percent((25, 50, 75, 100, 125, 150, 200, 300, 400))),
                option("cpu_clock_percentage", "CPU Clock", ENUM, "100",
                       choices=percent(range(25, 425, 25))),
                option("boost_mode", "Boost mode (CPU 1785 MHz, GPU 768 MHz)", BOOL, "false",
                       restart=True)),
        section("on_screen", "On-Screen",
                option("fps_counter_position", "FPS counter", ENUM, "hidden", prefix=False,
                       choices=HUD_POSITIONS),
                option("rendered_ir_position", "Rendered resolution", ENUM, "hidden",
                       prefix=False, choices=HUD_POSITIONS),
                option("shader_notice", "Shader Compile Notice", BOOL, "false"))),
    tab("graphics", "Graphics",
        section("backend", "Backend",
                option("spirv_shader_gen", "SPIR-V Shader Generation", BOOL, "true", True),
                option("disable_spirv_optimizer", "Disable SPIR-V Optimizer", BOOL, "true",
                       True)),
        section("shaders", "Shaders",
                option("use_hw_shader", "Hardware Shader", BOOL, "true"),
                option("shaders_accurate_mul", "Accurate Shader Multiplication", BOOL, "true"),
                option("use_shader_jit", "Shader JIT", BOOL, "true"),
                option("async_shader_compilation", "Async Shader Compilation", BOOL, "false"),
                option("use_disk_shader_cache", "Disk Shader Cache", BOOL, "true")),
        section("threading", "Threading",
                option("async_gpu_emulation", "Async GPU", BOOL, "false", True),
                option("async_presentation", "Async Presentation", BOOL, "true")),
        section("presentation", "Presentation",
                option("use_vsync", "VSync", BOOL, "false"),
                option("use_display_refresh_rate_detection", "Detect Display Refresh Rate",
                       BOOL, "true"),
                option("use_skip_duplicate_frames", "Skip Duplicate Frames", BOOL, "true")),
        section("frame_generation", "Frame Generation",
                option("use_frame_generation", "Frame Generation (LSFG)", BOOL, "false"),
                option("frame_generation_multiplier", "Frame Gen Max Multiplier", ENUM, "2",
                       choices=choices(("2x", "2"), ("3x", "3"), ("4x", "4")),
                       depends_on=FRAME_GEN),
                option("frame_generation_high_refresh", "Frame Gen Above 60 Hz", BOOL, "false",
                       depends_on=FRAME_GEN),
                option("frame_generation_performance_mode", "Frame Gen Performance Mode", BOOL,
                       "false", depends_on=FRAME_GEN),
                option("frame_generation_flow_scale", "Frame Gen Flow Scale", ENUM, "25",
                       choices=percent((25, 50, 75, 100)), depends_on=FRAME_GEN)),
        section("accuracy", "Accuracy",
                option("simulate_3ds_gpu_timings", "Simulate 3DS GPU Timings", BOOL,
                       "false"))),
    tab("enhancements", "Enhancements",
        section("resolution", "Resolution",
                option("resolution_factor", "Internal Resolution", ENUM, "1",
                       choices=RESOLUTIONS),
                # the resolution while docked, switching as the console is docked or undocked
                option("docked_resolution", "Docked Resolution", ENUM, "same",
                       choices=choices(("Same as handheld", "same")) + RESOLUTIONS),
                option("use_integer_scaling", "Integer Scaling", BOOL, "false")),
        section("filtering", "Filtering",
                option("filter_mode", "Linear Filtering", BOOL, "true"),
                option("texture_filter", "Texture Filter", ENUM, "0", choices=TEXTURE_FILTERS),
                option("texture_sampling", "Texture Sampling", ENUM, "0",
                       choices=TEXTURE_SAMPLING),
                option("anisotropic_filtering", "Anisotropic Filtering", ENUM, "0",
                       choices=ANISOTROPY)),
        section("custom_textures", "Custom Textures",
                option("custom_textures", "Custom Textures", BOOL, "false"),
                option("preload_textures", "Preload Custom Textures", BOOL, "false"),
                option("async_custom_loading", "Async Custom Texture Loading", BOOL, "true"),
                option("dump_textures", "Dump Textures", BOOL, "false", True))),
    tab("stereo", "3D",
        section("stereoscopic", "Stereoscopic 3D",
                option("render_3d", "Stereoscopic 3D", ENUM, "0", choices=STEREO),
                option("factor_3d", "Stereoscopic Depth", ENUM, "0",
                       choices=percent(range(0, 110, 10))),
                option("swap_eyes_3d", "Swap Eyes", BOOL, "false"),
                option("mono_render_option", "Eye Rendered In 2D", ENUM, "0",
                       choices=MONO_EYE),
                option("disable_right_eye_render", "Disable Right Eye Render", BOOL, "false")),
        section("labo_vr", "Nintendo Labo VR",
                option("cardboard_screen_size", "Labo VR Image Size", ENUM, "85",
                       choices=percent(range(30, 105, 5))),
                option("cardboard_x_shift", "Labo VR Horizontal Align", ENUM, "35",
                       choices=percent(range(-100, 105, 5))),
                option("cardboard_y_shift", "Labo VR Vertical Align", ENUM, "0",
                       choices=percent(range(-100, 105, 5))))),
    tab("audio", "Audio",
        section("output", "Output",
                option("audio_emulation", "Audio Emulation", ENUM, "0", True,
                       choices=AUDIO_EMULATION),
                option("volume", "Volume", ENUM, "1",
                       choices=choices(*((f"{v}%", "1" if v == 100 else
                                          ("0" if v == 0 else f"{v / 100:g}"))
                                         for v in range(0, 110, 10)))),
                option("simulate_headphones_plugged", "Simulate Headphones", BOOL, "false")),
        section("timing", "Timing",
                option("enable_audio_stretching", "Audio Stretching", BOOL, "true"),
                option("enable_realtime_audio", "Realtime Audio", BOOL, "false"))),
    tab("layout", "Layout",
        section("screens", "Screens",
                option("screen_layout", "Screen Layout", ENUM, "0", choices=SCREEN_LAYOUTS),
                option("swap_screens", "Swap Screens", BOOL, "false"),
                option("screen_gap", "Screen Gap", ENUM, "0",
                       choices=choices(*((f"{v} px", str(v))
                                         for v in (0, 4, 8, 16, 24, 32, 48, 64, 96, 128)))),
                option("large_screen_proportion", "Large Screen Proportion", ENUM, "4",
                       choices=choices(*((f"{v:.2f}x", f"{v:g}")
                                         for v in [1.0 + 0.5 * i for i in range(15)]))),
                option("stretch_fullscreen", "Stretch Fullscreen", BOOL, "false")),
        section("screen_overlay", "Screen Overlay",
                option("overlay_screen_position", "Overlay Screen Position", ENUM, "2",
                       choices=SMALL_SCREEN_POSITIONS, depends_on=OVERLAY_LAYOUT),
                option("overlay_screen_size", "Overlay Screen Size", ENUM, "25",
                       choices=percent(range(10, 65, 5)), depends_on=OVERLAY_LAYOUT),
                option("overlay_screen_opacity", "Overlay Screen Opacity", ENUM, "100",
                       choices=percent(range(10, 110, 10)), depends_on=OVERLAY_LAYOUT))),
    tab("controls", "Controls",
        section("buttons", "Button Mapping",
                button("a", "A Button", 0),
                button("b", "B Button", 1),
                button("x", "X Button", 2),
                button("y", "Y Button", 3),
                button("up", "D-Pad Up", 4),
                button("down", "D-Pad Down", 5),
                button("left", "D-Pad Left", 6),
                button("right", "D-Pad Right", 7),
                button("l", "L Button", 8),
                button("r", "R Button", 9),
                button("start", "START", 10),
                button("select", "SELECT", 11),
                button("zl", "ZL Button", 12),
                button("zr", "ZR Button", 13),
                button("toggle_pointer", "Toggle Touch Pointer", 14),
                button("cycle_layout", "Cycle Screen Layout", 15),
                button("touch_tap", "Touch Tap (pointer mode)", 13),
                button("swap_screens", "Swap Screens Button", 16)),
        section("touch_pointer", "Touch Pointer",
                option("pointer_source", "Touch Pointer Source", ENUM, "0",
                       choices=POINTER_SOURCES)),
        section("motion", "Motion",
                option("gyro_source", "Gyro Source", ENUM, "0", choices=GYRO_SOURCES),
                option("gyro_orientation", "Gyro Orientation", ENUM, "0",
                       choices=GYRO_ORIENTATIONS),
                option("gyro_sensitivity_x", "Gyro Sensitivity X", ENUM, "100",
                       choices=percent(range(50, 310, 25))),
                option("gyro_sensitivity_y", "Gyro Sensitivity Y", ENUM, "100",
                       choices=percent(range(50, 310, 25))))),
    tab("system", "System",
        section("console", "Console",
                option("is_new_3ds", "New 3DS", BOOL, "false", True),
                option("region_value", "System Region", ENUM, "-1", True, choices=REGIONS),
                option("apply_region_free_patch", "Region Free Patch", BOOL, "true", True)),
        section("profile", "Profile",
                option("username", "Username", TEXT, "", True, max_length=10),
                option("language", "System Language", ENUM, "1", True,
                       choices=LANGUAGES_3DS)),
        section("clock", "Clock",
                option("init_clock", "Clock Source", ENUM, "0", True, choices=INIT_CLOCK)),
        section("modules", "Modules",
                option("lle_applets", "LLE Applets", BOOL, "true", True),
                option("enable_required_online_lle_modules", "Required Online LLE Modules",
                       BOOL, "false", True)),
        section("plugins", "Plugins",
                option("plugin_loader", "Plugin Loader", BOOL, "false", True),
                option("allow_plugin_loader", "Allow Games To Change Plugin Loader", BOOL,
                       "true", True)),
        section("sd_card", "SD Card",
                option("use_virtual_sd", "Virtual SD Card", BOOL, "true", True),
                option("async_fs_operations", "Async Filesystem Operations", BOOL, "true",
                       True))),
    tab("advanced", "Advanced",
        section("cpu", "CPU",
                option("use_cpu_jit", "CPU JIT", BOOL, "true", True),
                option("fastmem", "Fastmem", BOOL, "false", True)),
        section("speed_hacks", "Speed Hacks",
                option("movie_cpu_throttle", "Slower CPU During Movies", BOOL, "false"),
                option("movie_cpu_clock_percentage", "CPU Clock During Movies", ENUM, "45",
                       choices=percent((25, 35, 45, 55, 65, 75)), depends_on=MOVIE_THROTTLE),
                option("skip_slow_draw", "Skip Slow Draws", BOOL, "false"),
                option("skip_texture_copy", "Skip Uncached Texture Copies", BOOL, "false"),
                option("skip_cpu_write", "Skip Small CPU Writes", BOOL, "false"))),
]

# What the frontend says itself, by key (src/tico)
EXTRA_STRINGS = {
    P + "compiling_shaders": "Compiling shaders",
    # a notice, followed by the file's path
    P + "frame_gen_missing_dll": "Frame generation needs Lossless.dll at",
    # the overlay's live list holds Dekopon's Amiibo and camera pictures
    "emulator_portal": "Amiibo & Camera",
    "dekopon_amiibo": "Amiibo",
    "dekopon_amiibo_load": "Load",
    "dekopon_amiibo_empty": "No .bin files in amiibo/",
    "dekopon_amiibo_scan": "Scan a real Amiibo",
    "dekopon_amiibo_on_reader": "On the reader",
    "dekopon_amiibo_waiting": "Waiting...",
    "dekopon_amiibo_overwrite": "Save anyway, erasing",
    "dekopon_press_again": "Press again",
    "dekopon_amiibo_remove": "Remove active Amiibo",
    "dekopon_amiibo_no_nfc": "The NFC service is unavailable",
    "dekopon_amiibo_already_active": "An Amiibo is already active",
    "dekopon_amiibo_not_scanning": "The game is not scanning for Amiibo",
    "dekopon_amiibo_load_failed": "Could not load the Amiibo file",
    "dekopon_amiibo_loaded": "Amiibo loaded",
    "dekopon_amiibo_none_active": "No Amiibo is active",
    "dekopon_amiibo_removed": "Amiibo removed",
    "dekopon_camera": "Camera",
    "dekopon_camera_target": "Feed image to",
    "dekopon_camera_in_use": "In use",
    "dekopon_camera_empty": "No .png/.jpg files in camera/",
    "dekopon_camera_clear": "Show nothing",
    "dekopon_camera_bad_image": "Cannot use that image",
    "dekopon_camera_set": "Cameras now show this image",
    "dekopon_camera_cleared": "Cameras cleared",
}


def value_key(label: str) -> str:
    """settings_dekopon_value_ and the label in lower case, as tico_config.cpp builds it."""
    return P + "value_" + re.sub(r"[^a-z0-9]+", "_", label.lower()).strip("_")


def translated(text: str) -> tuple[str, ...]:
    return (text,) + tuple(TRANSLATIONS.get(text, (text,) * (len(LANGUAGES) - 1)))


def build_settings() -> OrderedDict:
    settings = OrderedDict(
        core_id="dekopon",
        display_name="Dekopon",
        config_file="dekopon.jsonc",
        slugs=["3ds", "dekopon"],
        is_libretro=False,
        bool_true_value="true",
        bool_false_value="false",
        tabs=[],
    )
    for tab_slug, sections in TABS:
        settings["tabs"].append(OrderedDict(
            name=P + tab_slug,
            sections=[OrderedDict(title=P + slug, options=options) for slug, options in sections]))
    return settings


def build_strings(settings) -> dict[str, dict[str, str]]:
    strings = {lang: {} for lang in LANGUAGES}
    for slug, text in LABELS.items():
        for lang, value in zip(LANGUAGES, translated(text)):
            strings[lang][P + slug] = value
    labels = {choice["label"] for tab in settings["tabs"] for section in tab["sections"]
              for entry in section["options"] for choice in entry.get("choices", [])}
    for label in sorted(labels & TRANSLATIONS.keys()):
        for lang, value in zip(LANGUAGES, translated(label)):
            strings[lang][value_key(label)] = value
    for key, text in EXTRA_STRINGS.items():
        for lang, value in zip(LANGUAGES, translated(text)):
            strings[lang][key] = value
    return strings


def main():
    settings = build_settings()
    keys = [entry["key"] for tab in settings["tabs"] for section in tab["sections"]
            for entry in section["options"]]
    duplicates = sorted({key for key in keys if keys.count(key) > 1})
    if duplicates:
        raise SystemExit(f"keys listed twice: {duplicates}")

    SETTINGS.write_text(json.dumps(settings, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
    owned = (P, "dekopon_") + OLD_PREFIXES
    for lang, entries in build_strings(settings).items():
        path = LANG_DIR / f"{lang}.json"
        current = json.loads(path.read_text(encoding="utf-8"), object_pairs_hook=OrderedDict)
        merged = OrderedDict((k, v) for k, v in current.items()
                             if not k.startswith(owned) and k not in entries)
        merged.update(sorted(entries.items()))
        path.write_text(json.dumps(merged, indent=4, ensure_ascii=False) + "\n", encoding="utf-8")
    print(f"wrote {SETTINGS.relative_to(TICO.parent)} ({len(keys)} options) "
          f"and {len(LANGUAGES)} languages")


if __name__ == "__main__":
    main()
