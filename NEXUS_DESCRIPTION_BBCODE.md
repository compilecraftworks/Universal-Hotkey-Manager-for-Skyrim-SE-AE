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
[*][b]Accurate Skyrim remaps[/b] — Keys changed in Skyrim's own Controls menu are read from the live ControlMap and displayed accurately. If an effective root ControlMap_Custom.txt exists, live Skyrim controls are read-only in UHM; an exact loose controlmap.txt binding is editable only when no binary custom map can override it.
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

[size=4][b]Making Skyrim controls editable in UHM[/b][/size]

[code]controlmap.txt[/code] is Skyrim's readable base/input-context definition. The effective loose file is normally [code]Data/Interface/Controls/PC/controlmap.txt[/code] in the MO2 virtual Data tree. [code]ControlMap_Custom.txt[/code] is a separate binary override created in the Skyrim game root when a user changes bindings through Skyrim's own System > Controls menu; it is not intended for manual text editing.

UHM displays the resulting live bindings accurately, but keeps Skyrim controls read-only while an effective root [code]ControlMap_Custom.txt[/code] exists. To manage an installed loose [code]controlmap.txt[/code] through UHM:

[list=1]
[*]Close Skyrim completely.
[*]Back up, then delete [code]ControlMap_Custom.txt[/code] beside [code]SkyrimSE.exe[/code].
[*]Confirm that the intended, runtime-compatible [code]Data/Interface/Controls/PC/controlmap.txt[/code] wins in MO2's virtual Data tree.
[*]Start Skyrim and run a UHM scan. Matching loose controlmap bindings can then be edited with UHM's normal backup protection.
[/list]

Deleting the custom file restores the currently effective [code]controlmap.txt[/code], which may be a mod-provided file rather than Bethesda's archived default. Merely starting the game does not recreate the custom file; changing a binding again in Skyrim's Controls menu does, and UHM will return those live Skyrim bindings to read-only. Use a [code]controlmap.txt[/code] made for your Skyrim runtime because incompatible context layouts can cause broken controls or startup failure.

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

Automatic language mode follows the current Windows user language and locale (Korean, English, or Chinese), and Options can fix a language manually. UTF-8 mod names and paths remain supported without the optional Windows system-wide UTF-8 locale. UHM preserves SKSE Menu Framework's selected [code]PrimaryFont[/code] and unrelated settings while automatically enabling the Korean or Chinese glyph range required by the effective UI language. If this setting is changed, restart Skyrim once after the notice so Menu Framework can rebuild its font atlas. The selected font must contain the requested glyphs; UHM does not redistribute Windows fonts.

Press [b]Escape[/b] to close the manager. While a rename or shortcut-capture popup is active, Escape cancels that editor instead.

[size=4][b]Credits & License[/b][/size]

Universal Hotkey Manager for Skyrim SE-AE is licensed under [url=https://www.gnu.org/licenses/gpl-3.0.html][b]GNU GPLv3[/b][/url]. The complete source code, build scripts, and matching version tags are available on [url=https://github.com/compilecraftworks/Universal-Hotkey-Manager-for-Skyrim-SE-AE][b]GitHub[/b][/url]. Beginning with version 1.0.2, Nexus provides the MO2-ready Release ZIP only.

Credits to the authors of CommonLibSSE-NG, SKSE Menu Framework, Dear ImGui/cimgui, SKSE64, and Address Library for SKSE Plugins. Detailed third-party notices and license texts are included in the Release ZIP. All respective rights belong to their original authors.
```
