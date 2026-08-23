#pragma once

#include <filesystem>
#include <string_view>

namespace UHI::Writers
{
    class ConfigFileWriter
    {
    public:
        // Replaces one already-scanned scalar/array value while preserving the
        // file's surrounding syntax. The setting name, line and previous raw
        // value must all still match, preventing a stale UI row from changing
        // the wrong option. The first successful change creates .uhi.bak.
        [[nodiscard]] bool SetBinding(const std::filesystem::path& path,
            std::size_t lineNumber, std::string_view settingName,
            std::string_view expectedRaw, std::string_view newRaw) const;
    };
}
