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
        // v13 invalidates old modifier scopes and editability flags. The binary
        // record layout is unchanged from v11/v12, still readable for history.
        // Keep restored UI snapshots in lockstep with scanner/cache semantics.
        static constexpr std::uint32_t kSchemaVersion = 13;

        [[nodiscard]] bool Save(const std::filesystem::path& path,
            std::span<const HotkeyRecord> records) const noexcept;
        [[nodiscard]] std::optional<std::vector<HotkeyRecord>> Load(
            const std::filesystem::path& path, bool validateFingerprints = true) const noexcept;
    };
}
