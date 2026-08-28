# Universal Hotkey Manager for Skyrim SE-AE 1.0.6 - Installation

## Requirements

- Skyrim Special Edition or Anniversary Edition
- SKSE64 appropriate for the installed game runtime
- Address Library for SKSE Plugins
- SKSE Menu Framework 2.1.1 or later

Menu Framework 2.x/3.x embeds ImGui in `SKSEMenuFramework.dll`. UHM does not require or load a separate `imgui.dll`.

The in-game interface defaults to automatic Windows-language detection and supports Korean, English, and Chinese. **Options > Appearance** controls overall text size, main-window opacity, and can fix a language manually. UTF-8 mod names and filesystem paths remain supported without enabling Windows' optional system-wide UTF-8 locale. UHM preserves Menu Framework's `PrimaryFont` and other settings while enabling only the Korean or Chinese glyph range required by the selected language. If UHM changes that range, restart Skyrim once after the notice so Menu Framework can rebuild its font atlas. The configured primary font must contain the requested glyphs.

## Mod Organizer 2

1. Install `Universal Hotkey Manager for Skyrim SE-AE 1.0.6.zip` as a normal mod.
2. Make sure SKSE Menu Framework is installed and enabled.
3. Enable Universal Hotkey Manager for Skyrim SE-AE after its requirements.
4. Start the game through SKSE.
5. Press `Del` to open the manager, open **Options**, then press **Start full scan**.

Press `Escape` to close the manager. If a rename, binding, or opening-shortcut capture is active, Escape cancels that input first.

UHM is ESP-free and does not require a new game. Initial startup performs only an exact virtual `SexLab.esm` presence check and a bounded previous-result read. After one requested full scan has established a validated snapshot, a completed save load schedules a cached incremental refresh; opening UHM alone does not rescan. Unchanged virtual files are reused and there is no frame-by-frame filesystem monitoring.

## Skyrim controlmap ownership

The loose, readable Skyrim mapping is `Data/Interface/Controls/PC/controlmap.txt`. `ControlMap_Custom.txt` beside `SkyrimSE.exe` is a binary override created after changing a binding in Skyrim's own Controls menu. UHM displays the resulting live keys but treats them as read-only whenever that custom file is effective.

To edit an installed loose controlmap through UHM, close Skyrim, back up and delete the root `ControlMap_Custom.txt`, confirm the intended runtime-compatible loose `controlmap.txt` wins in MO2's virtual Data tree, then start Skyrim and run a full scan. Starting the game alone does not recreate the custom file; using Skyrim's Controls menu to change a binding does. Deleting it restores the currently effective loose or archived controlmap values, not necessarily Bethesda defaults. Do not use a controlmap built for an incompatible Skyrim runtime.

## Updating

Replace the previous UHM files with the files from the new release. Version 1.0.6 contains only the SKSE plugin and minimal license documentation; it does not create an ESP or load-order entry.

## Files

- `SKSE/Plugins/UniversalHotkeyManager.dll` - main plugin
- `SKSE/Plugins/UniversalHotkeyManager.ini` - opening key, exact left/right Ctrl/Shift/Alt chord, UI scale, and UI language
- `README.md` - feature and safety documentation
- `CHANGELOG.md` - release changes
- `CHANGELOG_KO.md` - 한글 업데이트 이력
- `ThirdPartyLicenses/` - notices required by statically linked libraries

The scan cache is created at virtual `Data/SKSE/Plugins/UniversalHotkeyManager/scan-cache-v1.bin`. Under MO2 this normally lands in Overwrite. It may be deleted safely; the next scan rebuilds it.

The validated previous-result snapshot is stored beside it as `last-scan-v1.bin`. It may also be deleted safely. UHM ignores it automatically if its format is invalid or any fingerprinted evidence file changed or disappeared.

Both cache files are written immediately after a successful scan. They are independent of Skyrim save files, so no manual or automatic game save is required. If an incremental refresh detects newly added or changed active hotkeys, UHM shows a single undimmed count notification when the manager is visible; unchanged results produce no popup.

## Opening-key recovery

If the configured opening key conflicts, edit virtual `Data/SKSE/Plugins/UniversalHotkeyManager.ini`. With MO2 this is normally `Overwrite\SKSE\Plugins\UniversalHotkeyManager.ini`.

- `ToggleKey=0xD3` is Delete in DirectInput scan-code form.
- `Ctrl`, `Shift`, and `Alt` enable exact modifier matching.
- `CtrlKey`, `ShiftKey`, and `AltKey` can preserve a left/right modifier scan code.
- `Language=Automatic`, `Korean`, `English`, or `Chinese` controls UI language.
- `UiScale=0.80` through `1.35` controls overall text size.
- `WindowOpacity=0.35` through `1.00` controls only the main UHM window background.

Close Skyrim before editing or deleting the INI. Deleting it restores Delete, 100% text size, 90% window opacity, and automatic language selection.

## Removal

Disable or remove the UHM mod in MO2. UHM does not add forms to save games. Reports and logs under the active SKSE log directory may be removed separately if desired.
