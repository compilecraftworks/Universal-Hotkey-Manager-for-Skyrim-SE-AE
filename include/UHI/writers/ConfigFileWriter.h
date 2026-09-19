#pragma once

#include <filesystem>
#include <string_view>
#include <string>
#include <optional>

namespace UHI::Writers
{
    class ConfigFileWriter
    {
    public:
        [[nodiscard]] std::optional<std::string> ReadBinding(const std::filesystem::path& path,
            std::size_t lineNumber, std::string_view settingName, std::string_view settingSection = {}) const;
        // Replaces one already-scanned scalar/array value while preserving the
        // file's surrounding syntax. JSON records use a "json:" pointer in
        // settingSection; legacy records require a unique name on the line.
        // The previous value must still match. The first change creates .uhi.bak.
        [[nodiscard]] bool SetBinding(const std::filesystem::path& path,
            std::size_t lineNumber, std::string_view settingName,
            std::string_view expectedRaw, std::string_view newRaw, std::string_view settingSection = {}) const;
    };
}
