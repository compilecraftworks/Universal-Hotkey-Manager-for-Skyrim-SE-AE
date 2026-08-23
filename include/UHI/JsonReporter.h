#pragma once

#include "UHI/HotkeyRecord.h"

#include <filesystem>
#include <span>

namespace UHI
{
    class JsonReporter
    {
    public:
        void Write(const std::filesystem::path& outputPath, std::span<const HotkeyRecord> records) const;
    };
}
