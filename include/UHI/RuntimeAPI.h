#pragma once

namespace UHI::RuntimeAPI
{
    // Marks the native IMenu as available to external callers.  This is kept
    // separate from DLL load so consumers cannot open UHM before SKSE has
    // finished registering its UI host.
    void SetReady(bool ready) noexcept;
    [[nodiscard]] bool IsNativeHotkeyEnabled() noexcept;
}
