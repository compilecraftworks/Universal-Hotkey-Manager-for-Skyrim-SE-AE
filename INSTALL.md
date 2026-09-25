# Universal Hotkey Manager for Skyrim SE-AE 1.1.2 - Installation

## Requirements

- Skyrim SE 1.5.97, or AE 1.6.317 / 318 / 323 / 342 / 353 / 629 / 640 / 659 / 678 / 1130 / 1170 / 1179
- Skyrim 1.7.x and VR are not supported.
- SKSE64 appropriate for the installed game runtime
- Address Library for SKSE Plugins

UHM owns its Skyrim `IMenu` and statically links Dear ImGui's Win32/DX11 backends. It does not require SKSE Menu Framework or a separate `imgui.dll`.

The in-game interface defaults to automatic Windows-language detection and supports Korean, English, and Chinese. **Options > Appearance** controls overall text size, main-window opacity, and can fix a language manually. UHM loads the required Windows font and Unicode glyph range directly, so UTF-8 mod names and filesystem paths do not require Windows' optional system-wide UTF-8 locale.

## Mod Organizer 2

1. Install `Universal Hotkey Manager for Skyrim SE-AE 1.1.2.zip` as a normal mod.
2. Enable Universal Hotkey Manager for Skyrim SE-AE after SKSE64 and Address Library.
3. Start the game through SKSE.
4. Press `Del` to open the manager, open **Options**, then press **Start full scan**.

Press `Escape` to close the manager. If a rename, binding, or opening-shortcut capture is active, Escape cancels that input first.

UHM is ESP-free and does not require a new game. Initial startup performs only an exact virtual `SexLab.esm` presence check and a bounded previous-result read. After one requested full scan has established a validated snapshot, a completed save load schedules a cached incremental refresh; opening UHM alone does not rescan. Unchanged virtual files are reused and there is no frame-by-frame filesystem monitoring.

## Skyrim controlmap ownership

The loose, readable Skyrim mapping is `Data/Interface/Controls/PC/controlmap.txt`. `ControlMap_Custom.txt` beside `SkyrimSE.exe` is a binary override created after changing a binding in Skyrim's own Controls menu. UHM displays the resulting live keys but treats them as read-only whenever that custom file is effective.

To edit an installed loose controlmap through UHM, close Skyrim, back up and delete the root `ControlMap_Custom.txt`, confirm the intended runtime-compatible loose `controlmap.txt` wins in MO2's virtual Data tree, then start Skyrim and run a full scan. Starting the game alone does not recreate the custom file; using Skyrim's Controls menu to change a binding does. Deleting it restores the currently effective loose or archived controlmap values, not necessarily Bethesda defaults. Do not use a controlmap built for an incompatible Skyrim runtime.

## Updating

Exit Skyrim completely, then replace the previous UHM files with the files from the new release. Version 1.1.2 contains only the SKSE plugin, its INI and UI assets, and minimal license documentation; it does not create an ESP or load-order entry. Existing settings and binding history can be retained.

When upgrading from 1.1.0 or earlier, run a scan from **Options** once to rebuild results with the corrected setting scopes and editability flags introduced in 1.1.1. The existing `.uhi.bak` files, custom action names and binding history remain usable. Updating from 1.1.1 to 1.1.2 does not require a rescan or a change to Windows language/font settings.

## Files

- `SKSE/Plugins/UniversalHotkeyManager.dll` - main plugin
- `SKSE/Plugins/UniversalHotkeyManager.ini` - opening key, exact left/right Ctrl/Shift/Alt chord, UI scale, and UI language
- `SKSE/Plugins/UniversalHotkeyManager/assets/` - packaged mouse and gamepad line art
- `LICENSE` - GPL-3.0-or-later license text
- `THIRD_PARTY_NOTICES.md` - third-party component notices
- `ThirdPartyLicenses/` - notices required by statically linked libraries

The runtime package does not include `README.md` or the changelogs. They are published in the GitHub source repository.

The scan cache is created at virtual `Data/SKSE/Plugins/UniversalHotkeyManager/scan-cache-v1.bin`. Under MO2 this normally lands in Overwrite. It may be deleted safely; the next scan rebuilds it.

The validated previous-result snapshot is stored beside it as `last-scan-v1.bin`. It may also be deleted safely. UHM ignores it automatically if its format is invalid or any fingerprinted evidence file changed or disappeared.

Both cache files are written immediately after a successful scan. They are independent of Skyrim save files, so no manual or automatic game save is required. If an incremental refresh detects newly added or changed active hotkeys, UHM shows a single undimmed count notification when the manager is visible; unchanged results produce no popup.

## Opening-key recovery

If the configured opening key conflicts, edit virtual `Data/SKSE/Plugins/UniversalHotkeyManager.ini`. With MO2 this is normally `Overwrite\SKSE\Plugins\UniversalHotkeyManager.ini`.

- `ToggleKey=0xD3` is Delete in DirectInput scan-code form.
- `Enabled=false` completely disables UHM's native shortcut; API launchers remain available. Use `Enabled=true` to recover it.
- `Ctrl`, `Shift`, and `Alt` enable exact modifier matching.
- `CtrlKey`, `ShiftKey`, and `AltKey` can preserve a left/right modifier scan code.
- `Language=Automatic`, `Korean`, `English`, or `Chinese` controls UI language.
- `UiScale=0.80` through `1.35` controls overall text size.
- `WindowOpacity=0.35` through `1.00` controls only the main UHM window background.
- `ReadableTheme=true` selects warm high-contrast text (default: false).
- Text uses the current Windows UI font, with Windows-installed Malgun Gothic for Korean and Microsoft YaHei/JhengHei for Chinese. UHM does not bundle or install font files.

Close Skyrim before editing or deleting the INI. Deleting it restores Delete, 100% text size, 90% window opacity, and automatic language selection.

## Removal

Disable or remove the UHM mod in MO2. UHM does not add forms to save games. Reports and logs under the active SKSE log directory may be removed separately if desired.

## Unbinding and restoring a hotkey

Open **Change hotkey**, choose **Unbind**, then **Save change**. Only formats with a verified unbound representation support this operation. UHM cannot add modifier storage to a mod that stores one scalar code; configure such chords through that mod's own modifier or virtual-key integration.

**Restore original backup** previews the original value from the adjacent `.uhi.bak`; Save restores just that setting. The **Backups** tab keeps up to 256 changes made by UHM 1.0.7 and later in `SKSE/Plugins/UniversalHotkeyManager/binding-history-v1.bin`. It also lets you restore an unbound action. Select a previous value and save. External edits or moved source lines require a fresh scan; UHM refuses a stale restore. For live MCM values, load the intended save before restoring and save the game afterward.

Right-click a key to browse all its actions when the hover summary says “more.”
