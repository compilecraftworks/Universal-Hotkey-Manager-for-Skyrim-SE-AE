#pragma once

#include "UHI/HotkeyRecord.h"
#include "UHI/ScanControl.h"

#include <filesystem>
#include <cstdint>
#include <vector>

namespace UHI::Scanners
{
    // Captured on Skyrim's main thread from BSInputDeviceManager. The worker
    // receives only an immutable module path and RVA; it never calls a sink or
    // dereferences a live game object.
    struct ActiveInputSinkTarget
    {
        std::filesystem::path modulePath;
        std::uint32_t processEventRva{};
        // Static .pdata scans already know the exact function end.  Passing it
        // through avoids searching the complete exception table once per
        // function (quadratic on large CommonLib DLLs).
        std::uint32_t processEventEndRva{};
    };

    class PeInputAnalyzer
    {
    public:
        // Read-only PE import and x86-64 analysis. The DLL is never loaded.
        [[nodiscard]] std::vector<HotkeyRecord> Scan(const std::filesystem::path& dll,
            const CancelCallback& cancel = {}) const noexcept;

        // Deep static pass for CommonLib/SKSE ButtonEvent handlers.  Results
        // are emitted only when the function proves both a ButtonEvent type
        // check and a concrete idCode comparison.
        [[nodiscard]] std::vector<HotkeyRecord> ScanStaticInputHandlers(
            const std::filesystem::path& dll, const CancelCallback& cancel = {}) const noexcept;

        // Analyze only ProcessEvent implementations proven to be registered in
        // the current BSInputDeviceManager sink list. PE bytes are read from
        // disk on the scan worker; the loaded DLL is never patched or invoked.
        [[nodiscard]] std::vector<HotkeyRecord> ScanActiveInputSinks(
            const std::vector<ActiveInputSinkTarget>& targets,
            const CancelCallback& cancel = {}) const noexcept;
    };
}
