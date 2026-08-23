#pragma once

#include <filesystem>
#include <string>
#include <string_view>

namespace UHI
{
    // std::filesystem::path is UTF-16 on Windows, while Dear ImGui, JSON and
    // UHM's persisted strings are UTF-8. These conversions never consult the
    // active ANSI code page or the optional Windows UTF-8 system-locale flag.
    [[nodiscard]] inline std::string PathToUtf8(const std::filesystem::path& path)
    {
        const auto value = path.generic_u8string();
        return { reinterpret_cast<const char*>(value.data()), value.size() };
    }

    [[nodiscard]] inline std::filesystem::path PathFromUtf8(const std::string_view value)
    {
        return std::filesystem::path(std::u8string(
            reinterpret_cast<const char8_t*>(value.data()), value.size()));
    }
}
