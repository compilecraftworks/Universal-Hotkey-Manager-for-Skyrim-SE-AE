#include "UHI/RuntimeAPI.h"

#include "UHI/MenuFrameworkAdapter.h"

#include <SKSE/SKSE.h>

#include <atomic>
#include <cstdint>

namespace
{
    enum class MenuRequest : std::uint8_t
    {
        none,
        open,
        close
    };

    std::atomic_bool g_ready{ false };
    std::atomic_bool g_nativeHotkeyEnabled{ true };
    std::atomic_bool g_dispatchScheduled{ false };
    std::atomic<MenuRequest> g_pendingRequest{ MenuRequest::none };

    void ProcessMenuRequests()
    {
        for (;;) {
            const auto request = g_pendingRequest.exchange(MenuRequest::none);
            if (request == MenuRequest::open) {
                UHI::OpenMenuFrameworkWindow();
            } else if (request == MenuRequest::close) {
                UHI::CloseMenuFrameworkWindow();
            }

            g_dispatchScheduled = false;
            if (g_pendingRequest.load() == MenuRequest::none) break;

            bool expected = false;
            if (!g_dispatchScheduled.compare_exchange_strong(expected, true)) break;
        }
    }

    bool QueueMenuRequest(const MenuRequest request) noexcept
    {
        if (!g_ready.load()) return false;

        g_pendingRequest = request;
        if (g_dispatchScheduled.exchange(true)) return true;

        const auto tasks = SKSE::GetTaskInterface();
        if (!tasks) {
            g_dispatchScheduled = false;
            g_pendingRequest = MenuRequest::none;
            return false;
        }

        try {
            tasks->AddUITask(ProcessMenuRequests);
            return true;
        } catch (...) {
            g_dispatchScheduled = false;
            g_pendingRequest = MenuRequest::none;
            return false;
        }
    }
}

namespace UHI::RuntimeAPI
{
    void SetReady(const bool ready) noexcept
    {
        g_ready = ready;
        if (!ready) {
            g_pendingRequest = MenuRequest::none;
            g_dispatchScheduled = false;
        }
    }

    bool IsNativeHotkeyEnabled() noexcept
    {
        return g_nativeHotkeyEnabled.load();
    }
}

#define UHM_API extern "C" __declspec(dllexport)

UHM_API bool UniversalHotkeyManager_Open()
{
    if (UHI::IsMenuFrameworkWindowOpen()) return true;
    return QueueMenuRequest(MenuRequest::open);
}

UHM_API void UniversalHotkeyManager_Close()
{
    if (!UHI::IsMenuFrameworkWindowOpen() && g_pendingRequest.load() != MenuRequest::open) return;
    static_cast<void>(QueueMenuRequest(MenuRequest::close));
}

UHM_API bool UniversalHotkeyManager_IsMenuOpen()
{
    return UHI::IsMenuFrameworkWindowOpen();
}

UHM_API void UniversalHotkeyManager_SetHotkeyEnabled(const bool enabled)
{
    g_nativeHotkeyEnabled = enabled;
}
