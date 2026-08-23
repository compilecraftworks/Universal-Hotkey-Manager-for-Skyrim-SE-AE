# Downloaded UI dependencies

The legacy 1.x UI SDK is retained under `.deps` only as historical reference:

- `.deps/imgui-docking` — official Dear ImGui docking branch
- `.deps/SKSE-Menu-Framework-SDK-main` — SKSE Menu Framework SDK

UHI no longer links either library. The project vendors the framework author's
official 2.x/3.x single-header client as `include/SKSEMenuFramework.h`; it
resolves functions from the installed `SKSEMenuFramework.dll` at runtime.
Configure the adapter with `-DUHI_ENABLE_MENU_FRAMEWORK=ON`.
