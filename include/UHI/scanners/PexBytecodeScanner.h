#pragma once

#include "UHI/HotkeyRecord.h"

#include <filesystem>
#include <string_view>
#include <vector>

namespace UHI::Scanners
{
    class PexBytecodeScanner
    {
    public:
        // Parses Skyrim's big-endian PEX container without executing Papyrus.
        // Any structural error rejects only this file.
        [[nodiscard]] std::vector<HotkeyRecord> ScanContent(
            const std::filesystem::path& source, std::string_view bytes) const noexcept;
    };
}
