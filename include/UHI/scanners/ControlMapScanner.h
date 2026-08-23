#pragma once

#include "UHI/HotkeyRecord.h"

#include <filesystem>
#include <string_view>
#include <vector>

namespace UHI::Scanners
{
    // A live ControlMap record may inherit a loose controlmap.txt writer only
    // when it still represents that exact source binding.  Skyrim Controls
    // menu remaps otherwise come from the engine-owned binary
    // ControlMap_Custom.txt and must remain read-only in UHM.
    [[nodiscard]] bool MatchesEditableControlMapSource(const HotkeyRecord& runtime,
        const HotkeyRecord& source, bool customMapPresent) noexcept;

    class ControlMapScanner
    {
    public:
        [[nodiscard]] std::vector<HotkeyRecord> Scan(const std::filesystem::path& path) const;
        [[nodiscard]] std::vector<HotkeyRecord> ScanContent(const std::filesystem::path& source,
            std::string_view content) const;
        // Bethesda's shipped main-gameplay table is stable and compiled into
        // UHI, so normal scans never open the base-game Interface BSA.
        [[nodiscard]] std::vector<HotkeyRecord> KnownGameDefaults(
            const std::filesystem::path& gameRoot) const;
    };
}
