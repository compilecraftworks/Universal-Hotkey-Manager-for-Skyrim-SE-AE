#pragma once

#include <Windows.h>

namespace UniversalHotkeyManagerAPI
{
    inline constexpr wchar_t DllName[] = L"UniversalHotkeyManager.dll";

    using OpenFn = bool (*)();
    using CloseFn = void (*)();
    using IsMenuOpenFn = bool (*)();
    using SetHotkeyEnabledFn = void (*)(bool enabled);

    struct Functions
    {
        OpenFn open{};
        CloseFn close{};
        IsMenuOpenFn isMenuOpen{};
        SetHotkeyEnabledFn setHotkeyEnabled{};

        [[nodiscard]] explicit operator bool() const noexcept
        {
            return open && close && isMenuOpen && setHotkeyEnabled;
        }
    };

    // Resolve UHM only after SKSE has loaded it. Consumers must not load the
    // plugin themselves with LoadLibrary because SKSE owns its lifetime.
    [[nodiscard]] inline Functions Resolve() noexcept
    {
        const HMODULE module = GetModuleHandleW(DllName);
        if (!module) return {};

        return {
            reinterpret_cast<OpenFn>(GetProcAddress(module, "UniversalHotkeyManager_Open")),
            reinterpret_cast<CloseFn>(GetProcAddress(module, "UniversalHotkeyManager_Close")),
            reinterpret_cast<IsMenuOpenFn>(GetProcAddress(module, "UniversalHotkeyManager_IsMenuOpen")),
            reinterpret_cast<SetHotkeyEnabledFn>(
                GetProcAddress(module, "UniversalHotkeyManager_SetHotkeyEnabled"))
        };
    }
}
