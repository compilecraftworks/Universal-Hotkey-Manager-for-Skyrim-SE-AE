# UI dependency provenance

UHM consumes the exact Dear ImGui version pinned by `vcpkg.json` and
`vcpkg-configuration.json`. The verified vcpkg build closure supplies Dear
ImGui and its official Win32/DX11 backends to the native Skyrim `IMenu` target.

Local historical SDK downloads under ignored `.deps` directories are not
build inputs, are not packaged, and are not committed. SKSE Menu Framework,
cimgui, and a standalone `imgui.dll` are not runtime dependencies.
