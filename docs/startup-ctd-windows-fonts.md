# Startup CTD investigation — Windows UI font collections

Date: 2026-09-25. Baseline: released UHM 1.1.1, commit
`043a0f6ab336a151d1346644cabaef8be256c5a1`.

Correction version: 1.1.2. Local correction and clean SE/AE build verified.
The release includes matching runtime/source ZIPs and bilingual changelogs.
In-game confirmation of the reporter's specific crash remains outstanding.

## Report and attribution

The report says Skyrim AE 1.6.1170 starts with UHM 1.0.6 but exits before launch
with every later version, including 1.1.1. That runtime is in UHM's explicit
support table. The reporter's crash log, UHM log, exact installed DLL and Windows
UI font are not available. This investigation proves a startup defect with the
same introduction boundary; it does not establish that this is the reporter's
specific crash.

The archived 1.0.6 source ZIP is the comparison baseline. The v1.0.6 Git tag does
not contain all changes shipped under that version number. The final archived
1.0.6 already uses the D3D initialization hook. Its 1.6.1170 relocation and call
offset are unchanged in 1.0.7/1.1.1. The three archived DLLs import the same DLL
names; this is not proof that every imported symbol resolves on the reporter's
system, but no new DLL dependency was found at the regression boundary.

## Reproduced defect

1.0.6 opens Windows font files. Starting in 1.0.7, the primary font follows the
Windows message/UI font using GDI. `GetFontData(dc, 0, ...)` returns data starting
at the selected face of a TrueType Collection (TTC), while the embedded SFNT
table offsets still refer to the complete collection. UHM passed this slice as
a standalone font to ImGui/stb. The resulting table lookups can address the
wrong bytes or go beyond the allocation during `BuildNativeFontAtlas`, called
from the startup D3D hook before the manager is opened.

For example, the installed Yu Gothic UI returns 13,908,012 bytes via table zero,
but the complete collection is 13,908,392 bytes. Its name table claims offset
13,906,860 and length 1,532, beyond the sliced buffer. Even offsets within the
buffer address the wrong data because the base moved by 380 bytes.

The probe compiles the exact archived 1.0.7/1.1.1 font header and the pinned
ImGui library. It substitutes only the requested logical font returned by
`SystemParametersInfoW`; it does not change Windows settings or launch Skyrim.
A test-only SEH boundary records access violations without Windows error dialogs.
No SEH recovery is added to production code.

| Windows UI font | Archived 1.1.1 | Corrected full atlas build |
| --- | --- | --- |
| Segoe UI | Pass | Pass |
| Microsoft YaHei UI | Build returns success; extraction still uses the wrong base | Pass |
| Microsoft JhengHei UI | Access violation `0xC0000005` | Pass |
| MS UI Gothic | Access violation `0xC0000005` | Pass |
| Yu Gothic UI | Access violation `0xC0000005` | Pass |

The archived 1.0.7 code also reproduces the access violation with MS UI Gothic
and Yu Gothic UI. A successful old build alone does not establish correct TTC
glyph data; the new regression test separately verifies the selected face.

## Correction

- Request the complete TTC with GDI's `ttcf` table selector. Use table zero for
  standalone fonts when the collection query is unavailable.
- Match the GDI-selected face's complete SFNT directory against the collection
  directories, then pass the matching `ImFontConfig::FontNo` to ImGui. A fixed
  index zero would silently substitute a different face on affected systems.
- Check header, collection count, directory and table bounds before handing the
  buffer to ImGui. Retain the existing 32 MiB font-data limit and font fallback.
- Use scoped GDI handles and font-buffer ownership, transferring the buffer to
  ImGui only after the font is accepted. Preserve font sizes, the compact atlas,
  Windows default selection and Korean/Chinese merges.
- Log entry into font-atlas building so a startup log can distinguish that stage
  from hook installation and completed renderer initialization.

This changes the font extraction path, not the supported game runtimes, engine
layouts, hooks, scanner, input routing, caches or dependency pins.

## Verification

- Clean SE/AE plugin build and all 31 CTest tests pass.
- Synthetic collection tests cover first/second face selection, the old sliced
  representation, incomplete directories, bad offsets, versions and counts.
- Nine installed Windows faces pass five load/build/destroy cycles each: Segoe
  UI, Malgun Gothic, Microsoft YaHei, Microsoft YaHei UI, Microsoft JhengHei,
  Microsoft JhengHei UI, MS Gothic, MS UI Gothic and Yu Gothic UI.
- Each face's name table equals the table independently read from the selected
  GDI font; UI variants correctly select collection index 1.
- Those cycles retain no additional tracked ImGui allocation or GDI object.
  The existing 20 full-atlas cycles remain stable at their warmed-up 10,332-byte
  static glyph-range cache, and all existing Korean/Chinese UI glyph checks pass.
- No Skyrim/MO2 or GPU execution has been performed for this correction. These
  tests do not establish absence of every possible game crash or leak.

Local evidence is under `output/startup-1.0.6-to-1.1.1/`:
`font-probe.log`, `font-before-after.log`, `font-tests.log`,
`build-and-tests.log`, the extracted archived headers and DLL import reports.

## Reporter follow-up

Request the `UniversalHotkeyManager.log`, `skse64.log`, and Crash Logger report
from the same failed 1.1.1 launch, plus Windows display language and any custom
system UI font. Confirm the exact loaded UHM DLL if multiple copies exist. A
missing crash report can itself indicate failure before Crash Logger loaded;
retain the UHM/SKSE logs rather than treating no crash log as no fault.

The release reply is in [reporter-reply-v1.1.2.md](reporter-reply-v1.1.2.md).

## References

- [Microsoft GetFontData documentation](https://learn.microsoft.com/en-us/windows/win32/api/wingdi/nf-wingdi-getfontdata)
  documents the difference between table zero and `ttcf` for collections.
- [Microsoft OpenType file/collection specification](https://learn.microsoft.com/en-us/typography/opentype/spec/otff#collections)
  defines collection face directories and file-relative table offsets.
