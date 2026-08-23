#pragma once

#include <filesystem>

namespace UHI
{
    // Known Bethesda base-game, DLC, bundled Creation Club/AE and shared
    // Creations files are outside UHI's scan scope. A loose controlmap.txt is
    // the sole exception because it is the effective MO2-virtual override.
    [[nodiscard]] bool IsLooseControlMap(const std::filesystem::path& path) noexcept;
    [[nodiscard]] bool IsKnownBethesdaGameFile(const std::filesystem::path& path) noexcept;
}
