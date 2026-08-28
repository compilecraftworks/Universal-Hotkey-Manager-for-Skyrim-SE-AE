#pragma once

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#endif

namespace UHI
{
    inline void EnterBackgroundScanMode() noexcept
    {
#ifdef _WIN32
        // Scans are explicitly user-triggered while the manager owns the menu
        // and a modal UI freezes gameplay. Do not use
        // THREAD_MODE_BACKGROUND_BEGIN here: it throttles disk I/O severely
        // and makes a large MO2 virtual Data tree appear stuck on one file.
        SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_NORMAL);
#endif
    }
}
