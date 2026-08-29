# Universal Hotkey Manager Menu and Hotkey API

Universal Hotkey Manager for Skyrim SE-AE exposes a small C ABI for external
hotkey managers and menu integrations. The API is available from
`UniversalHotkeyManager.dll` after SKSE has loaded and initialized UHM.
It was introduced in UHM 1.0.6.

## Exports

```cpp
extern "C" bool UniversalHotkeyManager_Open();
extern "C" void UniversalHotkeyManager_Close();
extern "C" bool UniversalHotkeyManager_IsMenuOpen();
extern "C" void UniversalHotkeyManager_SetHotkeyEnabled(bool enabled);
```

### `UniversalHotkeyManager_Open`

Requests that the UHM menu be opened. Returns `true` when the menu is already
open or when the request was accepted. The visible menu state may not update
until the next UI frame. Returns `false` if UHM has not finished registering
its native menu or if the request could not be queued.

### `UniversalHotkeyManager_Close`

Requests that the UHM menu be closed. It is safe to call repeatedly while the
menu is already closed. A pending open request is superseded by the close
request.

### `UniversalHotkeyManager_IsMenuOpen`

Returns whether the UHM menu is currently open. An accepted open request may
not become visible until the next UI frame.

### `UniversalHotkeyManager_SetHotkeyEnabled`

Enables or disables only UHM's native opening hotkey. External API control
remains available while the native hotkey is disabled. The setting is
runtime-only and resets to enabled whenever the game restarts.

## Resolving the API

Resolve the exports with `GetModuleHandleW` and `GetProcAddress` after SKSE has
loaded UHM. Do not call `LoadLibrary`: SKSE owns the plugin's loading,
initialization, and lifetime.

```cpp
HMODULE module = GetModuleHandleW(L"UniversalHotkeyManager.dll");
if (!module) {
    return;
}

using OpenFn = bool (*)();
auto open = reinterpret_cast<OpenFn>(
    GetProcAddress(module, "UniversalHotkeyManager_Open"));

if (open) {
    open();
}
```

The complete consumer declarations are in
[`extras/UniversalHotkeyManagerAPI.h`](../extras/UniversalHotkeyManagerAPI.h),
and a minimal integration example is in
[`tests/MenuApiConsumer.cpp`](../tests/MenuApiConsumer.cpp).

## Threading and lifetime

The exported functions are safe to invoke from an external plugin thread.
Open and close operations are marshalled onto SKSE's UI task queue. Do not
cache the DLL handle or function pointers across a game-process restart.
