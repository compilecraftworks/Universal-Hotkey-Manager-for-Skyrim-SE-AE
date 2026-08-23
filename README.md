# Universal Hotkey Manager for Skyrim SE-AE (UHM)

Universal Hotkey Manager for Skyrim SE-AE is an ESP-free Skyrim SE/AE SKSE plugin that inventories keyboard, mouse, and gamepad bindings, identifies their likely owners, and shows conflicts in a native ImGui manager window.

## Current status

Version `1.0.2` provides an on-demand, four-stage scanner and native Menu Framework UI. UHM never traverses the mod tree during plugin/game loading. Press `Del` to open the window, then start the scan explicitly.

The manager opens large on first use and remains freely resizable. Its tabs are ordered as **Overview**, the purpose categories (**Game, Environment, Character, Combat, SexLab when installed, Interface, External Tools**), and **Options** last. Overview summarizes conflicts and editability and displays the keyboard, mouse, and gamepad maps. Options places the start/cancel controls and live four-stage Scanner at the top, followed by the overall text-size and opening-shortcut controls. A validated previous-scan snapshot is restored automatically and is visibly marked as previous data until the user refreshes it.

The keyboard map remains resolution-independent vector UI. The mouse and Xbox-style gamepad use packaged high-resolution technical line illustrations with transparent backgrounds and enlarged device panels, with live binding-state outlines and the same full hover tooltips aligned over the actual controls. Images are loaded through Windows Imaging Component into Skyrim's existing D3D11 device; no standalone ImGui or image-library dependency is required. If an asset cannot be loaded, the manager safely falls back to its built-in vector device drawing.

The in-game UI follows the current Windows user language in **Automatic** mode (Korean, English, or Chinese) and can be fixed manually in **Options > Appearance**. The same section controls overall text size and the main-window background opacity without dimming text, controls, or device illustrations. Automatic detection checks both the Windows UI language and current user locale so Korean or Chinese installations that retain an English fallback LANGID are still identified correctly. UHM compiles and exchanges report/path text as UTF-8 and converts Windows filesystem paths explicitly, so the optional Windows system-wide UTF-8 locale setting is not required for mixed Korean, Chinese, and English mod paths. UHM also enables the required Korean or Chinese glyph range in the existing SKSE Menu Framework INI without changing its `PrimaryFont` or unrelated options. If that range had to be enabled, an in-game notice requests one Skyrim restart so Menu Framework can rebuild its font atlas. The selected Menu Framework font must contain the requested glyphs; UHM does not redistribute Windows fonts.

Opening the manager pauses gameplay until it closes. The on-demand scanner runs at normal I/O priority with four bounded workers while the menu is open, throttles only progress presentation, prunes known non-runtime asset trees, and reads each eligible mod BSA once per full scan. Loose setting candidates use a small marker probe before the complete file is read, and BSA JSON/XML resources are opened only when their path or filename indicates settings. Cancelling changes the status immediately and stops at the next safe file or archive-entry boundary.

`Del` is the default opening key. In **Options > Opening shortcut**, click the capture field and press any recognized keyboard key or left/right Ctrl, Shift, or Alt chord. The same page provides persistent text-size, language, and main-window opacity controls. Changes apply and persist in virtual `Data/SKSE/Plugins/UniversalHotkeyManager.ini` when the window closes; `WindowOpacity` accepts `0.35` through `1.00`, and modifier matching is exact, so plain `Del` does not also trigger on `Ctrl+Del`. A plain Escape closes UHM and consumes that press so Skyrim's system menu is not opened underneath it. While an editor or shortcut-capture field owns input, Escape cancels and closes only that popup without saving.

If the opening key conflicts and UHM cannot be opened, edit `Data/SKSE/Plugins/UniversalHotkeyManager.ini` (normally `MO2\Overwrite\SKSE\Plugins\UniversalHotkeyManager.ini`). `ToggleKey` is a DirectInput keyboard scan code (`0xD3` is Delete); `Ctrl`, `Shift`, and `Alt` enable exact modifiers, and the corresponding `*Key` value can distinguish left/right variants. Deleting the INI restores Delete, 100% text size, and automatic language selection.

Recovery path: open SKSE Menu Framework's own panel (normally `F1`), select **Universal Hotkey Manager for Skyrim SE-AE**, and press **Open shortcut manager**. This remains available if another plugin consumes the configured shortcut.

UHM primarily uses Skyrim input events. If an earlier input consumer prevents the configured key from reaching later event sinks, a focused-game-window key-state fallback handles only UHM's opening shortcut. It polls at low priority, never installs a function detour/signature hook, ignores input while Skyrim is not the foreground window, and shares a debounce with the event path.

The plugin requires:

- SKSE64 and Address Library/CommonLib-compatible game runtime
- SKSE Menu Framework 2.1.1 or later
- The Microsoft Visual C++ runtime normally required by SKSE plugins

Zydis, zlib, and LZ4 are linked statically into UHM.

UHM uses Menu Framework's exported ImGui API directly. It does not load or
require the legacy standalone `imgui.dll`; unrelated mods may still install
their own copy for their own purposes.

## Detection scope

1. **Configuration files**
   - compiled-in Skyrim main-gameplay defaults, with a loose `controlmap.txt` override when present
   - INI, JSON/JSONC, TOML, YAML, XML, CFG, CONF, SETTINGS, PROPERTIES, and relevant TXT files
   - UTF-8, UTF-16 LE/BE, and BOM-aware text
   - custom/camel-case setting names such as `KeyCustom`, `KeyDefine`, `PrimaryKeyDefine`, and equivalent XML element/attribute forms
   - ReShade, ENB, Community Shaders, SKSE plugin settings, MCM-generated settings, JContainers `JCUser` data
   - loose files and TES4 BSA v104/v105 archives, including zlib/LZ4 entries
2. **Scripts**
   - loose and archived PSC/PEX files
   - validated Skyrim PEX bytecode, including `RegisterForKey`, every SkyUI key-map option form, property/temporary/cast data flow, custom key getters, and integer-array options
   - unresolved string evidence remains visible as a non-conflicting candidate
3. **Native plugins**
   - chunked DLL string/config inspection without loading the DLL
   - bounded PE64 import and executable-section analysis with Zydis
   - constant keys passed to `GetAsyncKeyState`, `GetKeyState`, and `RegisterHotKey`, including modifiers
   - a main-thread snapshot of currently registered CommonLib `InputEvent` sinks followed by worker-only analysis of those exact `ProcessEvent` RVAs
   - `ButtonEvent` device/type/id comparisons, keyboard/mouse/XInput decoding, and referenced menu/ControlMap context evidence
4. **Saved & runtime registrations**
   - matching or newest SKSE co-save
   - exact saved SKSE `KEYR/REGS/REGE` Papyrus key registrations
   - structured persistent settings and bounded binary candidates
   - live integer properties and arrays from already-registered SkyUI MCM objects, joined to their PEX option labels and setting identities; exact resolved options are editable through the owning MCM handler or an unambiguous save-backed property
   - specialized semantic names and contexts for the 17 current SexLab Framework / SexLab Utility Plus `sslSystemConfig` hotkeys

PapyrusUtil's `StorageUtilData` is treated as a shared container, never as a mod owner. UHM extracts the first child namespace (or the root JSON filename) and resolves it against active ESP/ESL/ESM names, MCM identities, native plugins, and corroborating scanner owners. For example, `StorageUtilData/UD/...` is attributed to `UnforgivingDevices` when that active plugin is present. If no unique match exists, the namespace is shown instead of the misleading generic `StorageUtilData` label; the original evidence path remains preserved in the JSON report.

Every finding includes detector, source path, confidence, editability, effective-runtime state, conflict eligibility, activation context, and context confidence. The normal UI is an effective-binding view: it admits decoded settings from live Skyrim/SKSE/MCM/interface configuration locations plus specialized current ReShade, ENB, Community Shaders, controlmap sources, and keys recovered from currently registered native input handlers. Static PEX/DLL constants, archived defaults, profiles, presets, examples, backups, translations, UI style data, and arbitrary Data/game-root authoring INIs remain excluded until runtime evidence can prove that their handler is active. Guesses are never presented as confirmed conflicts. Internally UHM expands each scope into possible simultaneously active engine states and intersects those sets: common menu/item layers, nested gameplay/SexLab/free-camera states, and external overlays are represented without exposing the internal matrix in the UI. A red conflict requires the same physical input, two confirmed activation contexts, and a proven non-empty context intersection. Unknown+unknown, confirmed+unknown, and inferred-context pairs are amber overlaps rather than confirmed conflicts. Proven field/menu or ordinary-field/SexLab-scene separation is not counted as a conflict. The list includes sortable **Conflict**, **Overlapping action**, and **Overlapping mod** columns. The compact Scope column uses Skyrim-aligned labels such as Field, Menu common, Item menus, Inventory, Favorites, Map, Skills, System/MCM, Lockpick, Global, and Unknown; complete evidence remains in the tooltip.

Bindings intentionally reused by Skyrim's own `controlmap.txt` across engine contexts are one coherent game mapping and are never counted as self-conflicts. Inactive preset, backup, UI-style, and PSC source/reference files are excluded from the active inventory. Multiple scanners or files that corroborate the same owner/action/key are rendered once while every evidence source remains in the JSON report. The toolbar exposes independent **Conflicts** and **Overlaps** filters. Confirmed overlapping-context conflicts are red; uncertain same-key pairs are amber overlaps.

Controls proven to be navigation or actions inside an already-open custom mod UI (for example, SkyUI search/group controls, Photo Mode controls, next/previous tab, list scrolling, or accepting a selection) remain in the raw JSON report with `uiLocalOnly: true` but are omitted from the normal shortcut list and conflict counts. The shortcut that opens, closes, shows, or toggles that UI is always retained.

Bethesda base-game, DLC, bundled Creation Club/AE, and shared Creations-resource BSAs are never opened. Known Skyrim gameplay controls come from UHM's built-in table and are read-only unless a virtual loose `Data/Interface/Controls/PC/controlmap.txt` already exists. UHM overlays the live `RE::ControlMap`, so controls remapped in Skyrim's own Controls menu are displayed accurately. If an effective root `ControlMap_Custom.txt` exists, all live Skyrim controls remain read-only because equal key values cannot prove whether the binary custom map or the loose text map owns the active binding. Only when no custom map exists may an exact active loose `controlmap.txt` binding be edited with a first-write `.uhi.bak`. `ControlMap_Custom.txt` itself is deliberately excluded from writing. Effective loose INI, JSON/JSONC, TOML, YAML, CFG, CONF, SETTINGS, and PROPERTIES bindings are also editable from the shortcut list. The writer requires the original path, setting name, line, and previous value to still match, preserves surrounding syntax/comments, creates a first-write `.uhi.bak`, atomically replaces the file, and rescans. ReShade, ENB, and Community Shaders current settings use the same protected writer. A resolved active SkyUI MCM option is changed on Skyrim's main task: UHM calls the owning key-map change event when its option ID is proven, otherwise it writes only the exact unambiguous live integer property and requests a later MCM/page refresh. Document-backed MCM values are written to disk first and their live MCM mirror is synchronized when it is registered; if that active mirror cannot accept the edit, UHM verifies and restores the original document value instead of leaving the two sources inconsistent, then explicitly tells the user that the original hotkey was restored. A save-backed Papyrus/MCM edit without a persistent document source displays a separate notice explaining that the current game must be saved to keep the new key after the next load. A one-integer SKSE/MCM value cannot encode a modifier chord, so UHM refuses that write instead of persisting invalid text. XML, UTF-16 files, archives, inactive variants, and hard-coded PEX/DLL values remain read-only. Structured settings infer an activation context only from explicit scope/context wording.

### Skyrim `controlmap.txt` and `ControlMap_Custom.txt`

`Data/Interface/Controls/PC/controlmap.txt` is Skyrim's readable base/input-context definition. The game-root `ControlMap_Custom.txt` is a separate binary override created by Skyrim after a user changes a binding in **System > Controls**; it is not intended for manual text editing. UHM shows the resulting live mapping but deliberately keeps it read-only while that custom override exists.

To make an installed loose `controlmap.txt` the editable authority, close Skyrim, back up and delete `ControlMap_Custom.txt` beside `SkyrimSE.exe`, verify the intended runtime-compatible loose file wins in MO2's virtual Data tree, then start Skyrim and run a full UHM scan. Deleting the custom file restores the currently effective `controlmap.txt`, which may be supplied by a mod rather than Bethesda's archive. Starting Skyrim alone does not recreate the custom file; changing a key again through Skyrim's Controls menu does, at which point UHM returns those live controls to read-only. Never install a `controlmap.txt` designed for a different runtime/context layout.

### Input code formats

UHM records the decoded code system in every tooltip and JSON report instead of assuming that all numeric values are DirectInput codes:

- `controlmap.txt`: DirectInput keyboard scan codes, zero-based mouse IDs, and Skyrim/XInput gamepad masks; commas are alternatives and `+` forms a chord
- SKSE/Papyrus/SkyUI MCM: unified input codes 0-255 keyboard, 256-265 mouse, and 266-281 gamepad
- ReShade: Windows virtual-key code followed by Ctrl/Shift/Alt flags; preset-shortcut tuple lists are also decoded
- Community Shaders: legacy scalar Windows VK values and current `InputCombo` arrays/packed device values
- ENBSeries: decimal Windows virtual-key values in `[INPUT]`, including `KeyCombination` chords
- Win32 DLL APIs: Windows virtual-key values and `RegisterHotKey` modifier masks

Numeric interpretation follows an evidence hierarchy: specialized format, explicit setting/comment clue, API/import clue, then the convention most common for that source. MCM, SKSE-plugin, Data/BSA, JCUser, PEX, and co-save values fall back to the SKSE unified code space; shallow game-root native/overlay settings fall back to Windows VK. An embedded DLL value falls back to VK when the DLL exposes Win32 key-state/hotkey APIs, otherwise to the SKSE/CommonLib convention. These fallback results are shown as `inferred` and their `codeSystem` is labeled `source-convention fallback` or `DLL ... fallback`. Only values with no usable source convention remain encoding-unknown candidates.

## Runtime safety

- Scanning runs on a background worker and publishes one immutable result snapshot only after completion.
- Config, PEX, DLL, and mod-BSA work uses at most four bounded workers while the paused manager is open.
- Broad asset trees are not traversed: UHM scans shallow game/Data roots plus known configuration roots, `Data/Scripts`, `Data/SKSE/Plugins`, and top-level mod BSAs. Definite resource/reference trees such as meshes, textures, sound, video, strings, shaders, LOD, screenshots, documentation, logs, UI styles, inactive presets, and backups are pruned before candidate collection. PSC sources may remain as non-active raw evidence but never enter the physical shortcut inventory.
- A bounded persistent fingerprint cache stores both positive and negative per-file results. Unchanged MO2 virtual winners are reused on later scans; changed files are re-read automatically.
- Every successful scan is saved immediately to UHM's own cache and validated result snapshot; this is independent of Skyrim save games and does not require saving the character.
- After one full scan has established a validated snapshot, a completed save load schedules one cached incremental refresh. Merely opening UHM does not rescan. It performs no frame-by-frame monitoring: unchanged files are reused, and only new, removed, or fingerprint-changed evidence is reparsed.
- If that refresh finds newly added or changed active hotkeys, the next visible manager frame shows one undimmed notification with the changed count. No popup is created when nothing changed.
- The current file and four-stage progress are copied to the UI through short mutex-protected state updates.
- Cancel requests are checked during directory, archive, PEX, DLL, PE, and user-data traversal; cancelled partial results are not published.
- Locked, disappearing, oversized, and malformed files are isolated and skipped.
- File sizes, archive entries, bytecode instructions, PE sections/imports/instructions, co-save chunks, and collected records have explicit upper bounds.
- Manager projections and conflict maps are built once per completed scan. Filtering is cached until the user changes a filter.
- SexLab availability is detected with a single virtual `Data/SexLab.esm` existence check. During an explicit scan UHM reads only each active plugin's bounded TES4 header/`MAST` list, follows direct and transitive SexLab master dependencies, and applies that evidence as a category hint to matching plugin/BSA/MCM findings. ESP-less canonical `ssl*` scripts and established SLAnimLoader, SLSO/Separate Orgasm, SL Tools, Utility Plus, SLTriggers, SLATE, MatchMaker, Defeat, Scaler, Aroused, Devious Devices, ZaZ, and Apropos identities are also recognized.
- When the scan is started, one Skyrim main-task snapshots bounded integer properties, globals, and arrays from the SkyUI MCM objects that are already registered in `SKI_ConfigManager`. Nested settings objects are followed to a bounded depth, array-backed key options are expanded per live element, and scanning itself never calls a Papyrus function or changes a property. PEX option labels and setting identities are then joined to those current values on the background worker. Only an explicit user edit invokes the source-aware MCM writer described above. The known `sslSystemConfig` bridge additionally supplies precise SexLab action names and scene contexts.
- The same main task copies registered `BSInputDeviceManager` sink vtable entries while holding the event-source spin lock. It immediately releases all sink objects and passes only owning DLL paths plus `ProcessEvent` RVAs to the worker. UHM never calls a foreign sink; the worker reads the on-disk PE, uses x64 unwind ranges to bound each function when available, and performs no code patching.
- The input event sink is attached only after SKSE reports `InputLoaded`.
- UHM does not detour game/SKSE functions or install signature-dependent runtime hooks.

The last point is deliberate: UHM can resolve standard and custom MCM keys whose current values are stored in registered script properties, globals, or arrays, but a hard-coded `RegisterForKey(42)` has no setting identity to read. Such constants remain static PEX evidence until saved registry data corroborates them. A version-specific generic `RegisterForKey` detour would observe those calls directly, but is excluded from this CTD-safe build; saving and rescanning updates the exact co-save registry without an invasive hook.

## Reports

After a successful scan, UHM writes `UniversalHotkeyManager/reports/hotkeys.json` below the active SKSE log directory. The report retains full binding names while the manager uses compact labels and tooltips. UHM also writes two different cache files under virtual `Data/SKSE/Plugins/UniversalHotkeyManager/`: `scan-cache-v1.bin` stores per-file positive and negative scanner results, while `last-scan-v1.bin` stores the validated visible snapshot for immediate restoration. Under MO2 they normally appear in `Overwrite\SKSE\Plugins\UniversalHotkeyManager\`. Both may be deleted safely while the game is closed; the next full scan rebuilds them. They are written when scanning completes, not when Skyrim is saved.

## License and artwork

Universal Hotkey Manager for Skyrim SE-AE is free software licensed under the **GNU General Public License version 3 or later (GPL-3.0-or-later)**. The complete license is included as `LICENSE`.

Beginning with version 1.0.2, Nexus receives the MO2-ready **Release ZIP only**. The complete corresponding source, build scripts, version history, and notices are published at <https://github.com/compilecraftworks/Universal-Hotkey-Manager-for-Skyrim-SE-AE>. Use the Git tag matching the binary version (for example, `v1.0.2`) or GitHub's source archive for that tag. The Release ZIP still contains `LICENSE`, `THIRD_PARTY_NOTICES.md`, and every required third-party license text under `ThirdPartyLicenses/`.

The packaged `mouse.png` and `gamepad.png` are original line-art assets drawn specifically for UHM. They do not copy or redistribute artwork from another mod and are covered by the same GPL-3.0-or-later project license.

## Credits and third-party software

- [CommonLibSSE-NG](https://github.com/CharmedBaryon/CommonLibSSE-NG) - MIT
- [SKSE Menu Framework](https://github.com/Thiago099/SKSE-Menu-Framework) - MIT; runtime prerequisite, not redistributed
- [Dear ImGui](https://github.com/ocornut/imgui) and [cimgui](https://github.com/cimgui/cimgui) - MIT
- [fmt](https://github.com/fmtlib/fmt) and [spdlog](https://github.com/gabime/spdlog) - MIT
- [Xbyak](https://github.com/herumi/xbyak) and [rapidcsv](https://github.com/d99kris/rapidcsv) - BSD-3-Clause
- [LZ4](https://github.com/lz4/lz4) - BSD-2-Clause
- [zlib](https://zlib.net/) - zlib License
- [Zydis](https://github.com/zyantific/zydis) and [Zycore](https://github.com/zyantific/zycore-c) - MIT
- [SKSE64](https://skse.silverlock.org/) and [Address Library for SKSE Plugins](https://www.nexusmods.com/skyrimspecialedition/mods/32444) - runtime prerequisites, not redistributed

Detailed notices and the exact license texts shipped with the binary are listed in `THIRD_PARTY_NOTICES.md` and `ThirdPartyLicenses/`.

## Build and test

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\build.ps1 -WithSkse `
  -MenuFrameworkRoot "C:\path\to\SKSE-Menu-Framework-SDK"
```

The configured SKSE build currently runs 21 unit/integration tests covering configuration parsing, effective-runtime filtering and protected configuration writing, exhaustive DirectInput/SKSE/Windows-VK/XInput binding serialization and read-back, resource pruning, ReShade/Community Shaders/ENB formats, BSA decompression, real PEX parsing, co-save/JContainers data, PE input analysis, configurable opening-key persistence, persistent cache and previous-scan invalidation, SexLab TES4 master dependency classification, Bethesda-file exclusion, control-map alternatives/chords/editing, rendering/view projection, deduplication, and safety limits.

Create the MO2-ready Release ZIP and SHA-256 checksum from a verified build with:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\package.ps1
```

The script does not create a separate source ZIP. Corresponding source is provided by the matching public Git tag on GitHub. Generated builds, downloaded dependencies, local caches, and release artifacts are excluded from Git; third-party SDKs are not redistributed. Pass the Menu Framework SDK root when building the SKSE target.
# Universal Hotkey Manager for Skyrim SE-AE
