#pragma once

#include "UHI/HotkeyRecord.h"
#include "UHI/ScanControl.h"
#include "UHI/ScanCache.h"

#include <filesystem>
#include <string_view>
#include <vector>

namespace UHI::Scanners
{
    class PexScanner
    {
    public:
        [[nodiscard]] std::vector<HotkeyRecord> Scan(const std::filesystem::path& root,
            const CancelCallback& cancel = {}, const PathCallback& path = {}, ScanCache* cache = nullptr,
            std::size_t workers = 2, const ItemProgressCallback& itemProgress = {}) const;
        [[nodiscard]] std::vector<HotkeyRecord> ScanContent(const std::filesystem::path& source,
            std::string_view bytes) const;
        [[nodiscard]] static bool MayContainInputMarker(std::string_view bytes) noexcept;
    };
}
