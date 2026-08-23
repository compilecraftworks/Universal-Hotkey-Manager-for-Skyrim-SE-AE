# Runtime installation notes

UHI's DLL is self-contained for scanning and reporting. The ImGui window
requires SKSE Menu Framework 2.1.1 or later.

Install the framework's runtime files through its own MO2 mod entry, not by
copying them into the UHI mod:

- `SKSE Menu Framework\SKSE\Plugins\SKSEMenuFramework.dll`

UHI vendors the framework author's official 2.x/3.x single-header client. It
resolves the ImGui functions exported by `SKSEMenuFramework.dll` at runtime and
does not link the legacy 1.x `imgui.dll` or `SKSEMenuFramework.lib` SDK.

UHI itself remains ESP-free and does not launch Skyrim or MO2 during builds.

To configure the optional adapter from PowerShell:

```powershell
.\scripts\build.ps1 -WithSkse
```
