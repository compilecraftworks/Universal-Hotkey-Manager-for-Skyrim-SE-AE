# Universal Hotkey Manager for Skyrim SE-AE

[![ko-fi](https://ko-fi.com/img/githubbutton_sm.svg)](https://ko-fi.com/M1P225QD23)

An ESP-free SKSE plugin that scans the active Skyrim SE/AE mod environment and presents detected keyboard, mouse, and gamepad bindings in one manager.

## Highlights

- Four-stage on-demand scan for active settings, Papyrus/PEX, native plugins, and saved/runtime registrations
- Keyboard, mouse, and Xbox-style gamepad maps with categories, tooltips, conflicts, and overlaps
- Source-aware editing for supported loose configuration files and verified MCM-backed values
- Skyrim `controlmap.txt` support, including accurate read-only handling while `ControlMap_Custom.txt` is active
- Cached scan results restore immediately; later scans reuse unchanged files

## Requirements

- Skyrim Special Edition or Anniversary Edition
- SKSE64
- Address Library / CommonLib-compatible runtime
- [SKSE Menu Framework](https://www.nexusmods.com/skyrimspecialedition/mods/54909) 2.1.1+
- Microsoft Visual C++ Redistributable

## Install and use

1. Install the release ZIP with MO2 or Vortex.
2. Launch Skyrim through SKSE.
3. Press `Del` to open **Universal Hotkey Manager**.
4. Start a full scan from **Options**.

If the opening key conflicts, edit `Data/SKSE/Plugins/UniversalHotkeyManager.ini` (under MO2, normally `Overwrite/SKSE/Plugins/`).

## Documentation

- [Installation](INSTALL.md)
- [Detection scope](docs/detection-scope.md)
- [Runtime installation](docs/runtime-install.md)
- [Changelog](CHANGELOG.md) | [한국어 변경 이력](CHANGELOG_KO.md)
- [Third-party notices](THIRD_PARTY_NOTICES.md)

## License and credits

UHM is licensed under [GPL-3.0-or-later](LICENSE). Full corresponding source is available on [GitHub](https://github.com/compilecraftworks/Universal-Hotkey-Manager-for-Skyrim-SE-AE).

Built with CommonLibSSE-NG, Dear ImGui, SKSE, Address Library, Zydis, zlib, LZ4, and other components listed in the third-party notices. The packaged mouse and gamepad line-art assets are original UHM artwork.
