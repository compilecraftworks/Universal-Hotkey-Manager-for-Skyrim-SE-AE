Subject: [Universal Hotkey Manager 1.0.6] External Menu and Hotkey API

Hello,

Universal Hotkey Manager 1.0.6 provides a small C API for external hotkey and menu-management plugins.

Available API functions:

```cpp
extern "C" bool UniversalHotkeyManager_Open();
extern "C" void UniversalHotkeyManager_Close();
extern "C" bool UniversalHotkeyManager_IsMenuOpen();
extern "C" void UniversalHotkeyManager_SetHotkeyEnabled(bool enabled);
```

Function behavior:

`UniversalHotkeyManager_Open`

- Requests that the UHM menu be opened.
- Returns `true` if the menu is already open or the request was accepted.
- The visible menu state may not update until the next UI frame.
- Returns `false` if UHM has not finished registering its native menu or the request could not be queued.

`UniversalHotkeyManager_Close`

- Requests that the UHM menu be closed.
- It is safe to call repeatedly when the menu is already closed.

`UniversalHotkeyManager_IsMenuOpen`

- Returns whether the UHM menu is currently open.

`UniversalHotkeyManager_SetHotkeyEnabled`

- Enables or disables only UHM's native opening hotkey.
- External API control remains available when the native hotkey is disabled.
- This setting is runtime-only and resets to enabled when the game restarts.

Please resolve the functions with `GetModuleHandleW` and `GetProcAddress` after SKSE has loaded UHM. External plugins should not load UHM separately with `LoadLibrary`.

```cpp
HMODULE module = GetModuleHandleW(L"UniversalHotkeyManager.dll");
if (module == nullptr) {
    return;
}

using OpenFn = bool (*)();
auto open = reinterpret_cast<OpenFn>(
    GetProcAddress(module, "UniversalHotkeyManager_Open"));

if (open != nullptr) {
    open();
}
```

The source package includes:

- Consumer header: `extras/UniversalHotkeyManagerAPI.h`
- API documentation: `docs/UniversalHotkeyManager-Menu-API.md`
- `GetProcAddress` example: `tests/MenuApiConsumer.cpp`

Please let me know if you need any additional information for the integration.

Thank you.
