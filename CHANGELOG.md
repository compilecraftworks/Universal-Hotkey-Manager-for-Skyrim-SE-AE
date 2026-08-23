# Changelog

## 1.0.2 - 2026-08-23

- Fixed Escape ownership, popup routing, and input blocking so editors close without saving, UHM closes independently, and Skyrim's system menu does not open underneath it.
- Restored validated cached results before the first draw, limited automatic refresh to changed evidence after completed save loads, and prevented large DLL analysis from stalling stage 3 without reducing the remaining hotkey evidence passes.
- Applied live runtime ControlMap user bindings before loose and built-in defaults, while excluding console, debug, Creation/marketplace, and other unsupported control contexts.
- Refined Options and the keyboard, mouse, and gamepad views with saved opacity, consistent category styling, compact labels, clearer tooltips, and reliable rename/hotkey editors.
- Fixed Korean and Chinese glyph setup without requiring Windows' UTF-8 beta locale, while preserving Menu Framework's selected primary font and unrelated settings.
- Verified source-specific DirectInput, SKSE unified, Windows-VK, XInput/controlmap, overlay, document, and MCM write/rollback paths; completed GPLv3 release metadata and GitHub source publication.

## 1.0.1 - 2026-08-23

- Fixed lost Rename and Change Hotkey popup requests from Device View and every manager table.
- Made edit and notification popups persistent until opened, one-shot after dismissal, centered, undimmed, input-blocking, and cancellable with Escape.
- Added popup diagnostics, multi-binding tooltips, source-aware serialization checks, final screenshots, and matching English/Korean documentation.

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
