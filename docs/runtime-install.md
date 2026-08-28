# Runtime installation notes

UHM is an ESP-free SKSE plugin with its own Skyrim `IMenu`. Dear ImGui and the
official Win32/DX11 backends are statically linked into
`UniversalHotkeyManager.dll`; neither SKSE Menu Framework nor a standalone
`imgui.dll` is required or loaded.

Install the release through MO2 or Vortex after SKSE64 and Address Library.
UHM does not launch Skyrim or MO2 during builds.

To build the native plugin from PowerShell:

```powershell
.\scripts\build.ps1 -WithSkse
```
