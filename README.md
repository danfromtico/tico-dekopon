<picture>
<source media="(prefers-color-scheme: dark)" srcset="https://i.imgur.com/8qsV6MH.png">
<source media="(prefers-color-scheme: light)" srcset="https://i.imgur.com/4cpzGnB.png">
<img src="https://i.imgur.com/8qsV6MH.png" width="200">
</picture>

*Part of the tico ecosystem* - https://www.ticoverse.com

**Dekopon** is an open-source Nintendo 3DS emulator for the Nintendo Switch by [@PalindromicBreadLoaf](https://github.com/PalindromicBreadLoaf), based on Azahar.

This fork adapts Dekopon to work with the tico frontend as a tico module, keeping Dekopon's renderer, JIT and performance work as they are.

----------

## Summary

This fork focuses on making Dekopon part of tico without changing how it emulates.

It adds:

- Custom overlay matching tico design, including time, date, user avatar, and game title
- Quick menu with save and load state, cheats, Amiibo and camera, settings, restart and exit
- Dekopon's full settings, laid out in Dekopon's own tabs, in tico and in game, with per-game settings
- Built-in save and load state support, with pictures, auto save and continue where you left off
- Saves and states in tico's folders, like the other cores
- Playtime per account, shown in tico
- Tico chainload integration for returning to the frontend

On top of Dekopon it also:

- Builds with the mesa-switch Vulkan driver of the switch-dev image
- Compiles shaders on a second thread, on core 3 when the system grants it, for fewer freezes
- Turns anisotropic filtering off by default

----------

## Installing

Extract `tico-dekopon-module.zip` from the [latest release](https://github.com/danfromtico/tico-dekopon/releases/latest) into `sdmc:/tico/modules/` (you get `sdmc:/tico/modules/dekopon/`).

- System files (NAND, sysdata, cheats, mods and texture packs in `load/`, logs): `sdmc:/tico/system/dekopon/`
- Saves: `sdmc:/tico/saves/3ds/`
- States: `sdmc:/tico/states/3ds/`

----------

## Content Support

Dekopon is intended for legally obtained Nintendo 3DS software and user-provided system data.

- It does not bypass Nintendo 3DS encryption
- It expects decrypted, user-owned content in supported formats
- It does not include games, firmware, keys, BIOS files, or copyrighted Nintendo data

----------

## Frame Generation

Settings > Graphics > Frame Generation interpolates extra frames with Lossless Scaling's frame generation, through lsfg-vk.

Its shaders come from Lossless Scaling itself, which is not included: copy `Lossless.dll` from your own copy of Lossless Scaling to `sdmc:/tico/system/lsfg/Lossless.dll`, the folder tico's other cores share.

lsfg-vk is GPL-3.0-or-later, so builds with frame generation are GPL-3.0-or-later as a whole.

----------

## Building

```sh
./build_tico_module.sh
```

It builds `tico-dekopon.nro` in `ghcr.io/autorunhq/switch-dev:2026.10.12` and packs `build_tico/tico-dekopon-module.zip`. Docker is required.

----------

## Credits

This port is built on top of Dekopon, and through it on the official Azahar emulator project.

All core emulation work belongs to [@PalindromicBreadLoaf](https://github.com/PalindromicBreadLoaf) and the Azahar team and its contributors, including the Citra, PabloMK7 Citra, and Lime3DS projects that Azahar is based on.

- **[Dekopon](https://github.com/PalindromicBreadLoaf/dekopon)** by [@PalindromicBreadLoaf](https://github.com/PalindromicBreadLoaf)
- **[Azahar](https://github.com/azahar-emu/azahar)** by the Azahar team - [https://azahar-emu.org](https://azahar-emu.org/)
- **[dolphin-nx](https://github.com/NaGaa95/dolphin-nx)** by [@NaGaa95](https://github.com/NaGaa95): frame generation (LSFG) on Switch
- **[raikopon](https://github.com/Raibatsu/raikopon)** by [@Raibatsu](https://github.com/Raibatsu): the background shader JIT and the speed hacks, through Dekopon
- **[lsfg-vk](https://github.com/PancakeTAS/lsfg-vk)** by [@PancakeTAS](https://github.com/PancakeTAS): the frame generation backend
- Original Dekopon logo by [StickyMangoRice](https://stickymangorice.com/)

----------

## A Note

A lot of work in this scene disappears over time - not because it lacked value, but because it was never shared.

If you are building something, consider releasing it. Even small contributions can help others move forward.

----------

## Support

If you enjoy PalindromicBreadLoaf's work on Dekopon and want to support them:

[![ko-fi](https://ko-fi.com/img/githubbutton_sm.svg)](https://ko-fi.com/palindromicbreadloaf)
