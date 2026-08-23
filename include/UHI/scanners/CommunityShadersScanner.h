#pragma once

#include "UHI/HotkeyRecord.h"

#include <filesystem>
#include <vector>

namespace UHI::Scanners
{
    class CommunityShadersScanner
    {
    public:
        std::vector<HotkeyRecord> Scan(const std::filesystem::path& settingsPath) const;
    };
}
