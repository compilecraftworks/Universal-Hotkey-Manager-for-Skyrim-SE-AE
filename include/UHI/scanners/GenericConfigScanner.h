#pragma once

#include "UHI/HotkeyRecord.h"
#include "UHI/ScanControl.h"
#include "UHI/ConfigBindingParser.h"
#include "UHI/ScanCache.h"

#include <filesystem>
#include <string_view>
#include <vector>

namespace UHI::Scanners
{
    class GenericConfigScanner
    {
    public:
        // The game root is deliberately shallow-scanned: Data, SKSE and MCM are
        // supplied separately by the pipeline and would otherwise be scanned twice.
        [[nodiscard]] std::vector<HotkeyRecord> Scan(const std::filesystem::path& root,
            bool recursive = true, const CancelCallback& cancel = {}, const PathCallback& path = {},
            NumericCodeSpace numericFallback = NumericCodeSpace::unknown, ScanCache* cache = nullptr,
            std::size_t workers = 2, const ItemProgressCallback& itemProgress = {}) const;

        // Shared by loose files and BSA/archive entries.
        [[nodiscard]] std::vector<HotkeyRecord> ScanContent(const std::filesystem::path& source,
            std::string_view utf8Content, std::string_view owner = {},
            NumericCodeSpace numericFallback = NumericCodeSpace::unknown) const;

        [[nodiscard]] static bool MayContainBinding(std::string_view bytes) noexcept;
    };
}
