#pragma once

#include "UHI/HotkeyRecord.h"
#include "UHI/ScanControl.h"
#include "UHI/ScanCache.h"

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace UHI::Scanners
{
    class BsaScanner
    {
    public:
        // requestedStage limits extraction to config or script entries. Archives
        // are read-only and malformed entries are skipped independently.
        [[nodiscard]] std::vector<HotkeyRecord> Scan(const std::filesystem::path& root,
            ScanStage requestedStage, const CancelCallback& cancel = {}, const PathCallback& path = {},
            ScanCache* cache = nullptr, std::size_t workers = 2,
            const ItemProgressCallback& itemProgress = {}) const noexcept;

        // Reads each archive index only once and extracts both configuration
        // and script findings. The records retain their original stages.
        [[nodiscard]] std::vector<HotkeyRecord> ScanAll(const std::filesystem::path& root,
            const CancelCallback& cancel = {}, const PathCallback& path = {},
            ScanCache* cache = nullptr, std::size_t workers = 2,
            const ItemProgressCallback& itemProgress = {}) const noexcept;

        // Reads one named entry from a mod archive without materializing it.
        // Bethesda game/DLC/CC/AE/Creations archives are always rejected.
        [[nodiscard]] std::optional<std::string> ExtractText(const std::filesystem::path& archive,
            const std::filesystem::path& internalPath) const noexcept;
    };
}
