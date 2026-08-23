#pragma once

#include "UHI/HotkeyRecord.h"

#include <filesystem>
#include <vector>

namespace UHI::Scanners
{
    class EnbScanner
    {
    public:
        std::vector<HotkeyRecord> Scan(const std::filesystem::path& iniPath) const;
    };
}
