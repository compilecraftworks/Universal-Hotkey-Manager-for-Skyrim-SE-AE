# Changelog

## 1.0.2 - 2026-08-23

- Made Escape ownership deterministic: editor and notification popups consume the press and close only themselves, while Escape with no popup closes UHM and cannot open Skyrim's system menu underneath it.
- Reworked Options > Appearance into compact text-size, interface-language, and main-window-opacity rows. The new opacity preference is saved automatically without fading controls, device art, text, or modal popups.
- Restored the validated hotkey snapshot before the first window draw and limited automatic refresh to invalidated evidence after completed save loads, so opening UHM no longer starts a redundant scan.
- Read live runtime ControlMap bindings before loose `Interface/Controls/PC/controlmap.txt` and built-in defaults, so controls changed in Skyrim are reflected immediately. Console, debug, Creation/marketplace, and other excluded control contexts are filtered from both runtime and fallback results.
- Bounded native `ProcessEvent` disassembly to prevent unusually large DLLs such as SKSE Menu Framework from stalling stage 3, while retaining the complete DLL string, import, symbol, comparison, and active-sink evidence passes used for hotkey discovery.
- Finalized the readable device UI: black key surfaces, gray empty-key outlines, category-colored occupied outlines and tabs, no amber overlap outline, compact action text, matched keyboard/mouse/gamepad typography, canonical D-pad and mouse side-button labels, category-aware multi-binding tooltips, and dark option separators.
- Hardened rename and hotkey editor routing from Device View context menus and all sortable manager tables. Read-only records do not open a hotkey editor; supported DirectInput, SKSE unified, Windows-VK, XInput/controlmap, ReShade, Community Shaders, ENB, document, and MCM writers retain source-specific serialization and transactional rollback.
- Completed GPL-3.0-or-later distribution metadata and third-party credits. Nexus now receives the Release ZIP only; complete corresponding source, build scripts, and notices are published in the public GitHub repository under the matching version tag. The Release ZIP retains the full project license and all required third-party license texts, and the packaged mouse/gamepad illustrations are identified as original GPL-covered UHM artwork.

## 1.0.1 - 2026-08-23

- Fixed the shared editor-popup request path so **Rename** and **Change Hotkey** open reliably from both Device View and every category/Overview Hotkey Manager table.
- Kept modal requests queued until ImGui confirms that the popup is visible, preventing requests from being lost while a context menu closes or Menu Framework changes popup stacks.
- Applied the same retained-request behavior to changed-hotkey and hotkey-write notifications. A dismissed notification is consumed once and does not reappear merely because UHM is reopened.
- Preserved modal input blocking, undimmed backgrounds, centered content and buttons, Escape-to-cancel without saving, category-aware multi-binding tooltips, and source-aware hotkey serialization.
- Added diagnostic log entries for queued and opened rename/hotkey editors to make any remaining framework-specific popup failure directly traceable.
- Added the final Overview, Hotkey Manager, and Options screenshots to the English Nexus BBCode and Korean HTML descriptions, and added a matching Korean changelog.

## 1.0.0 - 2026-08-23

- Renamed the public release to **Universal Hotkey Manager for Skyrim SE-AE** and finalized the ESP-free SKSE/Menu Framework distribution for Skyrim SE and AE.
- Completed the scalable Overview, Game, Environment, Character, Combat, SexLab, Interface, External Tools, and Options screens with category colors, device maps, sortable hotkey tables, search and device filters, conflict/overlap filters, responsive typography, automatic or manual Korean/English/Chinese UI language selection, and a recoverable `Del` opening shortcut.
- Added full keyboard, mouse, and Xbox-style gamepad visualization with readable callouts, compact canonical labels, category-colored ownership, modifier chords, multi-binding tooltips, left-click rename, right-click rename/hotkey actions, and context-aware popups that block underlying input. Escape cancels every popup without saving and closes the manager without opening Skyrim's system menu.
- Added a four-stage cancellable scanner for effective loose configurations and supported archives, Papyrus PEX/MCM state, active native CommonLib/SKSE handlers and DLL evidence, saved registrations, JContainers data, ReShade, Community Shaders, ENB, and other external tools. Bethesda base-game/DLC/CC archives are not opened; the built-in Skyrim control table is used unless an effective loose `controlmap.txt` overrides it.
- Expanded detection for DirectInput scan codes, SKSE unified keyboard/mouse/gamepad codes, Windows virtual keys, XInput/controlmap masks, ReShade tuples, Community Shaders packed inputs, ENB combinations, symbolic chords, structured INI/JSON/JSONC/TOML/YAML/XML/CFG/CONF/SETTINGS/PROPERTIES values, SkyUI MCM data flow, live Papyrus properties, co-save registrations, and bounded PE64/CommonLib input comparisons.
- Deepened PEX/MCM analysis for literal and computed key-map options, arrays, properties, assignments, casts, MCM Helper/custom getters, state handlers, and active SkyUI values. Added focused recovery for Defeat F2, Typing Mode F4, Conditional Expressions Extended F8, SexLab 1.63, SexLab Utility Plus, SLSO, and related ecosystems.
- Added semantic action-name recovery from option labels, nearby descriptions, sections, functions, properties, translations, runtime registrations, and owner evidence. Internal prefixes, raw localization tokens, URLs, numeric metadata, and ambiguous placeholders are suppressed or humanized; users can override an action name from either view.
- Added purpose classification using plugin master graphs, verified mod identities, filenames, scripts, DLLs, settings paths, and weighted action evidence. SexLab-dependent and scene-only controls are separated from ordinary gameplay; known UI-local navigation is excluded from normal conflict analysis.
- Reworked conflict analysis around physical device/key identity and effective activation-context intersection. Only two confirmed overlapping contexts form a red conflict; unknown or inferred pairs are shown as amber overlaps, while proven disjoint contexts and analog-axis versus stick-click inputs are not conflicts. Tables show the overlapping functions and mods.
- Added source-aware hotkey editing with transactional writes, stale-value checks, atomic replacement, and first-write `.uhi.bak` backups. DirectInput, SKSE unified, Windows-VK, XInput/controlmap, ReShade, Community Shaders, ENB, and supported symbolic formats are serialized back in their original representation; lossy or unsupported conversions are rejected.
- Added active MCM synchronization on Skyrim's main task. When a document-backed MCM cannot accept the same value, the document is rolled back and the user is told that the original hotkey was restored. Save-backed Papyrus/MCM changes explicitly tell the user that a game save is required.
- Added immediate scan persistence independent of Skyrim saves, a per-file fingerprint/result cache, a validated visible-result snapshot, and cheap incremental refresh after completed save loads. Old scan schemas are invalidated automatically; both cache files may also be deleted safely while the game is closed.
- Added CTD-conscious lifecycle guards for preload, revert, delete-game, scanner cancellation, modal input capture, VM/runtime evidence, CommonLib sink snapshots, and main-thread write dispatch. No runtime function detours or address patches are used.
- Added opening-key and language recovery through `Data/SKSE/Plugins/UniversalHotkeyManager.ini`, automatic preference saving on close, scan-cache documentation, quick-start instructions, screenshot placeholders, English Nexus BBCode, Korean HTML descriptions, installation instructions, and release/source packaging.
- Added and expanded automated regression coverage for scanners, cache invalidation, code-space decoding/encoding, source-aware writes, controlmap devices, MCM synchronization contracts, context/conflict handling, classification, UI settings, cancellation, and safety limits.

## 0.0.1 - 2026-08-14 (Prebuild)

- Established the ESP-free SKSE plugin, Menu Framework window, configurable opening shortcut, and first keyboard/mouse/gamepad overview.
- Added the initial four-stage document, PEX, DLL, saved-data, external-overlay, and loose `controlmap.txt` scanners with bounded background work, cancellation, cache reuse, and safe publication.
- Added the first DirectInput, SKSE, VK, XInput, ReShade, Community Shaders, ENB, MCM, JContainers, BSA, PEX, DLL, and co-save decoding paths.
- Added initial categories, search, device filters, conflict display, source evidence, Bethesda archive exclusions, packaging scripts, documentation, and automated tests.
