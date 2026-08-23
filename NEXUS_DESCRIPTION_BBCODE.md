# Universal Hotkey Manager for Skyrim SE-AE — Nexus Description

```bbcode
[size=5][b]Universal Hotkey Manager for Skyrim SE-AE[/b][/size]

[b]Scan. Detect. Manage.[/b]

Universal Hotkey Manager for Skyrim SE-AE (UHM) is an ESP-free SKSE plugin that brings your Skyrim hotkeys into one clear, in-game manager.

Scan your active setup to discover keyboard, mouse, and gamepad bindings from Skyrim controls, mod configuration files, MCM settings, scripts, native plugins, ReShade, ENB, Community Shaders, and more. Browse detected bindings by category, see their likely owner and source, and find genuine conflicts with Skyrim-aware activation-context analysis.

[center][img]https://staticdelivery.nexusmods.com/mods/1704/images/189244/189244-1787425787-562889350.png[/img]
[i]Overview and device maps[/i][/center]

[size=4][b]Features[/b][/size]

[list]
[*][b]One place for every hotkey[/b] — View detected keyboard, mouse, and gamepad bindings in a native in-game UI.
[*][b]Context-aware conflict detection[/b] — Marks a real conflict only when both activation contexts are confirmed and overlap. Unknown or inferred scopes remain amber overlaps; keys reused in proven-disjoint contexts are not conflicts.
[*][b]Broad mod support[/b] — Scans supported INI, JSON/JSONC, TOML, YAML, XML, CFG, CONF, SETTINGS, and PROPERTIES files, plus relevant MCM, script, archive, and plugin evidence.
[*][b]Visual device maps[/b] — See bindings directly on keyboard, mouse, and gamepad layouts.
[*][b]Safe supported edits[/b] — Edit effective loose configuration bindings directly from the manager. UHM preserves each source's DirectInput, SKSE unified, Windows-VK, XInput/controlmap, ReShade, Community Shaders, or symbolic representation, creates a .uhi.bak backup on the first write, replaces files atomically, then rescans. A lossy or unsupported conversion is refused.
[*][b]Cached and CTD-conscious[/b] — Run the first full scan on demand. Later completed save loads perform one cached incremental refresh; merely opening the manager does not rescan, unchanged files are reused, and nothing polls the filesystem every frame.
[*][b]Persistent results[/b] — Successful scans are saved immediately by UHM, independently of Skyrim saves. Changed active hotkeys produce one undimmed count notification; unchanged results remain silent.
[*][b]Reports[/b] — A detailed JSON report is written after each successful scan.
[/list]

[size=4][b]How to use[/b][/size]

[list]
[*]Open the manager with [b]Delete[/b] by default.
[*]Start a scan from [b]Options[/b].
[*]Review bindings, owners, sources, contexts, and conflicts.
[*]Use the category tabs, device map, and filters to find what you need.
[*]Change the opening shortcut, overall UI text size, interface language, or main-window opacity in [b]Options[/b].
[*]Left-click a mapped key to rename its action, or right-click for Rename action / Change hotkey. In the list, use the row Edit buttons.
[*]Escape closes UHM; inside any editor or capture popup it cancels and closes without saving.
[/list]

[center][img]https://staticdelivery.nexusmods.com/mods/1704/images/189244/189244-1787425813-693912486.png[/img]
[i]Sortable hotkey manager and source-aware edit controls.[/i][/center]

[size=4][b]Opening-key recovery[/b][/size]

If the configured key conflicts and UHM cannot be opened, close Skyrim and edit:
[code]Data/SKSE/Plugins/UniversalHotkeyManager.ini[/code]
With MO2 it is normally created at [code]Overwrite/SKSE/Plugins/UniversalHotkeyManager.ini[/code]. [code]ToggleKey=0xD3[/code] is Delete in DirectInput scan-code form. [code]WindowOpacity=0.90[/code] controls only the main window background. Deleting the INI restores Delete, 100% text size, 90% opacity, and automatic language selection.

[size=4][b]Cache reset[/b][/size]

[code]Data/SKSE/Plugins/UniversalHotkeyManager/scan-cache-v1.bin[/code]
[code]Data/SKSE/Plugins/UniversalHotkeyManager/last-scan-v1.bin[/code]

Under MO2 these normally land in [code]Overwrite/SKSE/Plugins/UniversalHotkeyManager/[/code]. The first stores per-file scan results; the second stores the validated visible snapshot. Close the game and delete either or both to force a clean rebuild on the next full scan.

[center][img]https://staticdelivery.nexusmods.com/mods/1704/images/189244/189244-1787425854-1132357725.png[/img]
[i]Scanner, text size, language, opacity, and opening-shortcut settings.[/i][/center]

[size=4][b]Requirements[/b][/size]

[list]
[*]Skyrim Special Edition / Anniversary Edition
[*]SKSE64
[*]Address Library / CommonLib-compatible runtime
[*]SKSE Menu Framework 2.1.1 or newer
[*]Microsoft Visual C++ Redistributable required by SKSE plugins
[/list]

[b]Notes[/b]

UHM is designed to report effective, active bindings rather than treating every static value found in a mod file as a confirmed conflict. Read-only evidence is still shown where useful, while only supported live configuration sources can be edited.

Automatic language mode follows the current Windows user language and locale (Korean, English, or Chinese), and Options can fix a language manually. UTF-8 mod names and paths remain supported without the optional Windows system-wide UTF-8 locale.

Press [b]Escape[/b] to close the manager. While a rename or shortcut-capture popup is active, Escape cancels that editor instead.

[size=4][b]License and source[/b][/size]

UHM is free software released under [b]GPL-3.0-or-later[/b]. Beginning with version 1.0.2, Nexus provides the MO2-ready Release ZIP only. Complete corresponding source, build scripts, and version tags are published at [url=https://github.com/compilecraftworks/Universal-Hotkey-Manager-for-Skyrim-SE-AE]GitHub[/url]; select the tag matching the installed binary, such as [b]v1.0.2[/b]. The Release ZIP includes the complete UHM license, third-party notices, and required third-party license texts. The mouse and gamepad line-art images are original UHM assets released under GPL-3.0-or-later.

[size=4][b]Credits[/b][/size]

[list]
[*][url=https://github.com/CharmedBaryon/CommonLibSSE-NG]CommonLibSSE-NG[/url] - MIT
[*][url=https://github.com/Thiago099/SKSE-Menu-Framework]SKSE Menu Framework[/url] - MIT; required at runtime and not redistributed
[*][url=https://github.com/ocornut/imgui]Dear ImGui[/url] and [url=https://github.com/cimgui/cimgui]cimgui[/url] - MIT
[*][url=https://github.com/fmtlib/fmt]fmt[/url] and [url=https://github.com/gabime/spdlog]spdlog[/url] - MIT
[*][url=https://github.com/herumi/xbyak]Xbyak[/url] and [url=https://github.com/d99kris/rapidcsv]rapidcsv[/url] - BSD-3-Clause
[*][url=https://github.com/lz4/lz4]LZ4[/url] - BSD-2-Clause; [url=https://zlib.net/]zlib[/url] - zlib License
[*][url=https://github.com/zyantific/zydis]Zydis[/url] and [url=https://github.com/zyantific/zycore-c]Zycore[/url] - MIT
[*][url=https://skse.silverlock.org/]SKSE64[/url] and Address Library for SKSE Plugins - runtime prerequisites, not redistributed
[/list]
```
