#include "UHI/GameFilePolicy.h"
#include "UHI/PathEncoding.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <string>

namespace
{
    std::string Lower(std::string value)
    {
        std::ranges::transform(value, value.begin(), [](const unsigned char character) {
            return static_cast<char>(std::tolower(character));
        });
        return value;
    }

    std::string Normalized(const std::filesystem::path& path)
    {
        auto value = Lower(UHI::PathToUtf8(path));
        std::ranges::replace(value, '\\', '/');
        return value;
    }
}

namespace UHI
{
    bool IsLooseControlMap(const std::filesystem::path& path) noexcept
    {
        try {
            const auto value = Normalized(path);
            constexpr std::string_view suffix = "/interface/controls/pc/controlmap.txt";
            return value.ends_with(suffix) || value == "interface/controls/pc/controlmap.txt";
        } catch (...) {
            return false;
        }
    }

    bool IsKnownBethesdaGameFile(const std::filesystem::path& path) noexcept
    {
        try {
            if (IsLooseControlMap(path)) return false;
            const auto filename = Lower(UHI::PathToUtf8(path.filename()));
            if (filename.empty()) return false;
            if (filename == "controlmap_custom.txt") return true;
            if (filename.starts_with("skyrim - ") || filename.starts_with("cc")) return true;
            if (filename.starts_with("update.") || filename.starts_with("dawnguard.") ||
                filename.starts_with("hearthfires.") || filename.starts_with("dragonborn.")) return true;
            if (filename.starts_with("_resourcepack.") || filename.starts_with("marketplacetextures.")) return true;
            static constexpr std::array<std::string_view, 16> exact{
                "skyrim.esm", "skyrim.ccc", "skyrim_default.ini", "skyrim.ini", "skyrimprefs.ini",
                "high.ini", "medium.ini", "low.ini", "ultra.ini", "bethesdanet.ini",
                "creationkit.ini", "creationkitcustom.ini", "creationkitprefs.ini",
                "skyrimse.exe", "skyrimselauncher.exe", "steam_api64.dll"
            };
            return std::ranges::find(exact, filename) != exact.end();
        } catch (...) {
            // An unrepresentable path is not assumed to be Bethesda-owned.
            return false;
        }
    }
}
