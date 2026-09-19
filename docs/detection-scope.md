# Detection scope

Scanning is user-triggered from **Options**. UHM never performs a full mod-tree scan during plugin or game loading. A successful scan is saved immediately in UHM's own cache and result snapshot; it does not require a Skyrim save.

## Stage 1 — Configuration and archives

- Built-in Skyrim controls, overridden only by an active loose `Data/Interface/Controls/PC/controlmap.txt`
- Effective loose INI, JSON/JSONC, TOML, YAML, XML, CFG, CONF, SETTINGS, PROPERTIES, and relevant TXT settings
- ReShade, ENB, Community Shaders, current SKSE-plugin settings, and relevant JContainers data
- Dedicated hotkey documents and nested JSON/YAML binding maps are recognized even when individual action names omit “key.” Integral float exports such as `78.0` are accepted; fractional codes are rejected. Matching native DLL imports provide an inferred Windows-VK convention for dedicated native key documents when no explicit code-space declaration exists.
- Community Shaders settings are active only when its DLL is loaded, including when restoring an earlier snapshot.
- Mod BSAs only; Bethesda base-game, DLC, CC/AE, and shared Creations BSAs are excluded
- `ControlMap_Custom.txt`, `*_KID.ini`, resources, presets, backups, translations, logs, and files without input markers are excluded from the active inventory

## Stage 2 — Papyrus scripts and PEX

- Validated loose and archived PEX bytecode
- `RegisterForKey`, SkyUI key-map options, MCM Helper/custom getters, properties, globals, temporaries, casts, and integer arrays
- Active MCM runtime values are joined to PEX labels without invoking Papyrus functions during scanning
- Exact resolved live MCM options are editable; hard-coded PEX constants and unsupported or ambiguous runtime properties remain read-only

## Stage 3 — Native plugins and DLL

- Bounded PE64 import, string, executable-section, and `ProcessEvent` analysis without loading or patching the DLL
- Win32 key-state/hotkey APIs and CommonLib input-event device/type/id comparisons
- Only registered active CommonLib sinks are promoted into the normal inventory; unresolved static constants remain evidence

## Stage 4 — Saved registrations and external tools

- Exact SKSE co-save `KEYR/REGS/REGE` registrations
- Structured persistent settings and specialized current SexLab MCM values
- ReShade, ENB, Community Shaders, and other supported external-overlay bindings

## Editing

The manager includes mod-interface-only keys with an explicit scope label; they remain excluded from gameplay conflict analysis. The console launcher is visible and read-only, while console-internal and debug commands remain excluded. Right-clicking a device key opens the complete scrollable action list.

Unbind uses verified source-specific sentinels, never a universal zero. The editor can restore one setting from its original `.uhi.bak`. The Backups tab keeps up to 256 changes and verifies the expected current value before restoring an earlier value. Settings without a known disable convention remain guarded. UHM does not allocate Beyond VirtualKey IDs or replace its mapping/backup system.

Supported effective loose files and a loose `controlmap.txt` are editable. UHM serializes the captured key back into the record's original DirectInput, SKSE unified, Windows-VK, XInput/controlmap, ReShade, Community Shaders, ENB single-key, or symbolic representation. The writer verifies the original value and setting identity, creates a first-write `.uhi.bak`, atomically replaces the file, and rescans. JSON/JSONC records retain a full object/array path, so repeated setting names and harmless formatting changes remain editable. Legacy records without that path require a unique setting on their recorded line. Comments and unrelated string contents are not write targets. Matching registered SkyUI MCM values are synchronized on Skyrim's main task through the owning key-map event when possible, or through one exact unambiguous property with an explicit refresh notice. A queued MCM handler is shown as pending. Successful live changes are recorded only after the handler finishes and the actual value matches the request. If a finished handler does not apply the value after a linked document was written, UHM restores that document and reports the outcome. A handler still unfinished after 30 seconds is reported as unconfirmed; its linked document is retained because a rollback could race with a late script completion. The existing polling loop schedules verification, at most one MCM operation is pending, and callbacks hold only a weak receipt rather than VM objects. Closing UHM allows gameplay-dependent handlers to resume; their result notice is shown on the next opening. A successful save-backed Papyrus/MCM edit without a persistent document source tells the user that the game must be saved to retain it after the next load. A scalar SKSE/MCM value refuses modifier chords it cannot encode. If its modifier is stored in a separate sibling field, changing the main key preserves that modifier in the preview and file; changing the separate modifier is refused. INI/TOML edits re-resolve the selected section and key. Modifier inference is restricted to the same structured scope. Lossy cross-device conversion, archives, XML/UTF-16 writing, inactive variants, ambiguous MCM values, and hard-coded PEX/DLL values are refused and remain read-only.

## Incremental refresh

After the first full scan, a completed save load schedules one metadata-and-cache refresh. Opening the manager by itself restores the validated snapshot and does not rescan. Unchanged files reuse cached positive or negative results; only new, removed, or fingerprint-changed evidence is reparsed. There is no continuous filesystem watcher or frame-by-frame polling. Newly added or changed active bindings produce one undimmed notification the next time the manager is visible.
