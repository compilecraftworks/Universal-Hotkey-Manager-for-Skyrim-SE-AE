# Detection scope

Scanning is user-triggered from **Options**. UHM never performs a full mod-tree scan during plugin or game loading. A successful scan is saved immediately in UHM's own cache and result snapshot; it does not require a Skyrim save.

## Stage 1 — Configuration and archives

- Built-in Skyrim controls, overridden only by an active loose `Data/Interface/Controls/PC/controlmap.txt`
- Effective loose INI, JSON/JSONC, TOML, YAML, XML, CFG, CONF, SETTINGS, PROPERTIES, and relevant TXT settings
- ReShade, ENB, Community Shaders, current SKSE-plugin settings, and relevant JContainers data
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

Supported effective loose files and a loose `controlmap.txt` are editable. UHM serializes the captured key back into the record's original DirectInput, SKSE unified, Windows-VK, XInput/controlmap, ReShade, Community Shaders, ENB single-key, or symbolic representation. The writer verifies the original line and value, creates a first-write `.uhi.bak`, atomically replaces the file, and rescans. Matching registered SkyUI MCM values are synchronized on Skyrim's main task through the owning key-map event when possible, or through one exact unambiguous property with an explicit refresh notice. If live MCM synchronization fails after a linked document was written, UHM restores the verified original document value and explicitly reports that the original hotkey was restored. A successful save-backed Papyrus/MCM edit without a persistent document source tells the user that the game must be saved to retain it after the next load. A scalar SKSE/MCM value refuses modifier chords it cannot encode. Lossy cross-device conversion, archives, XML/UTF-16 writing, inactive variants, ambiguous MCM values, and hard-coded PEX/DLL values are refused and remain read-only.

## Incremental refresh

After the first full scan, loading a save or newly opening UHM schedules one metadata-and-cache refresh. Unchanged files reuse cached positive or negative results; only new, removed, or fingerprint-changed evidence is reparsed. There is no continuous filesystem watcher or frame-by-frame polling. Newly added or changed active bindings produce one undimmed notification the next time the manager is visible.
