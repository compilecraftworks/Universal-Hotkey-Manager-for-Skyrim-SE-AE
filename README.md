# Universal Hotkey Manager for Skyrim SE-AE

[![ko-fi](https://ko-fi.com/img/githubbutton_sm.svg)](https://ko-fi.com/M1P225QD23)

An ESP-free SKSE plugin that scans the active Skyrim SE/AE mod environment and presents detected keyboard, mouse, and gamepad bindings in one manager.

## Highlights

- Four-stage on-demand scan for active settings, Papyrus/PEX, native plugins, and saved/runtime registrations
- Name-independent structural detection for compound keyboard/gamepad fields, indirect setting names, Papyrus getter flows, MCM properties, and active native input handlers
- Keyboard, mouse, and Xbox-style gamepad maps with categories, tooltips, conflicts, and overlaps
- Source-aware editing for supported loose configuration files and verified MCM-backed values
- Skyrim `controlmap.txt` support, including accurate read-only handling while `ControlMap_Custom.txt` is active
- Cached scan results restore immediately; later scans reuse unchanged files
- External C API for opening and closing UHM or disabling only its native hotkey

## Requirements

- Skyrim Special Edition or Anniversary Edition
- SKSE64
- Address Library / CommonLib-compatible runtime
- Microsoft Visual C++ Redistributable

## Install and use

1. Install the release ZIP with MO2 or Vortex.
2. Launch Skyrim through SKSE.
3. Press `Del` to open **Universal Hotkey Manager**.
4. Start a full scan from **Options**.

If the opening key conflicts, edit `SKSE/Plugins/UniversalHotkeyManager.ini` inside the installed UHM mod. If MO2 has already created an overriding copy, edit the same relative path under `Overwrite` instead.

`ToggleKey` uses a DirectInput/DX keyboard scan code and accepts hexadecimal or decimal values. For example, `Delete=0xD3`, `F11=0x57`, and `F4=0x3E`. Modifier chords use `Ctrl`, `Shift`, or `Alt` plus the optional side-specific `CtrlKey`, `ShiftKey`, or `AltKey` fields. See the [Creation Kit DX scan-code table](https://ck.uesp.net/wiki/Input_Script#DXScanCodes); the distributed INI also contains this link and common examples as comments.

## External menu and hotkey API

External SKSE plugins can resolve these C exports from the already loaded `UniversalHotkeyManager.dll`:

```cpp
extern "C" bool UniversalHotkeyManager_Open();
extern "C" void UniversalHotkeyManager_Close();
extern "C" bool UniversalHotkeyManager_IsMenuOpen();
extern "C" void UniversalHotkeyManager_SetHotkeyEnabled(bool enabled);
```

Resolve the functions with `GetModuleHandleW` and `GetProcAddress` after SKSE has loaded UHM; do not load UHM separately with `LoadLibrary`. Disabling UHM's native opening hotkey leaves external API control available and resets to enabled when the game restarts. See the [API contract](docs/UniversalHotkeyManager-Menu-API.md), [consumer header](extras/UniversalHotkeyManagerAPI.h), and [minimal consumer example](tests/MenuApiConsumer.cpp).

## Documentation

- [Installation and cache guide](INSTALL.md)
- [Changelog](CHANGELOG.md) | [한국어 변경 이력](CHANGELOG_KO.md)
- [Third-party notices](THIRD_PARTY_NOTICES.md)
- [External menu and hotkey API](docs/UniversalHotkeyManager-Menu-API.md)

Additional development and distribution documentation is available in the [GitHub source repository](https://github.com/compilecraftworks/Universal-Hotkey-Manager-for-Skyrim-SE-AE).

## License and credits

UHM is licensed under [GPL-3.0-or-later](LICENSE). Full corresponding source is available on [GitHub](https://github.com/compilecraftworks/Universal-Hotkey-Manager-for-Skyrim-SE-AE).

Built with CommonLibSSE-NG, Dear ImGui, SKSE, Address Library, Zydis, zlib, LZ4, and other components listed in the third-party notices. Dear ImGui is statically linked into UHM; no separate UI framework or `imgui.dll` is required. The packaged mouse and gamepad line-art assets are original UHM artwork.
