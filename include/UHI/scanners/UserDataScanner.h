#pragma once

#include "UHI/HotkeyRecord.h"
#include "UHI/ScanControl.h"

#include <filesystem>
#include <string_view>
#include <vector>

namespace UHI::Scanners
{
    class UserDataScanner
    {
    public:
        // Reads persisted user files and the selected/current SKSE co-save.
        // documentsOverride exists for tests; an empty path uses the Windows
        // Known Documents folder (including MO2's virtualized view).
        [[nodiscard]] std::vector<HotkeyRecord> Scan(const std::filesystem::path& gameRoot,
            std::string_view preferredSaveName = {}, const CancelCallback& cancel = {},
            const std::filesystem::path& documentsOverride = {}, const PathCallback& path = {}) const noexcept;
    };
}
