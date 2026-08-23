#pragma once

#include "UHI/HotkeyRecord.h"
#include "UHI/ScanControl.h"
#include "UHI/ScanCache.h"

#include <filesystem>
#include <vector>

namespace UHI::Scanners
{
    class DllScanner
    {
    public:
        [[nodiscard]] std::vector<HotkeyRecord> Scan(const std::filesystem::path& root,
            const CancelCallback& cancel = {}, const PathCallback& path = {}, ScanCache* cache = nullptr,
            std::size_t workers = 2, const ItemProgressCallback& itemProgress = {}) const;
    };
}
