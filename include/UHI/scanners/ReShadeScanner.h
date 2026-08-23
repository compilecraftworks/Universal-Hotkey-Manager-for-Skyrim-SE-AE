#pragma once

#include "UHI/HotkeyRecord.h"

#include <filesystem>
#include <string>
#include <vector>

namespace UHI::Scanners
{
    class ReShadeScanner
    {
    public:
        [[nodiscard]] std::vector<HotkeyRecord> Scan(const std::filesystem::path& iniPath) const;
    };

    [[nodiscard]] std::string FormatReShadeBinding(const std::string& rawBinding);
}
