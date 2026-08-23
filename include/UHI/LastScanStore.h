#pragma once

#include "UHI/HotkeyRecord.h"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <vector>

namespace UHI
{
    class LastScanStore
    {
    public:
        // v7 persists effective-runtime state and exact structured-setting
        // locators used by the safe configuration writer.
        // Keep restored UI snapshots in lockstep with scanner/cache semantics.
        static constexpr std::uint32_t kSchemaVersion = 10;

        [[nodiscard]] bool Save(const std::filesystem::path& path,
            std::span<const HotkeyRecord> records) const noexcept;
        [[nodiscard]] std::optional<std::vector<HotkeyRecord>> Load(
            const std::filesystem::path& path, bool validateFingerprints = true) const noexcept;
    };
}
