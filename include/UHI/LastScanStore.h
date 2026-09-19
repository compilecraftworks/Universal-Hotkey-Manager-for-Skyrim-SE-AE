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
        // v12 invalidates pre-path JSON scan identities. The binary record
        // layout is unchanged from v11, which remains readable for history.
        // Keep restored UI snapshots in lockstep with scanner/cache semantics.
        static constexpr std::uint32_t kSchemaVersion = 12;

        [[nodiscard]] bool Save(const std::filesystem::path& path,
            std::span<const HotkeyRecord> records) const noexcept;
        [[nodiscard]] std::optional<std::vector<HotkeyRecord>> Load(
            const std::filesystem::path& path, bool validateFingerprints = true) const noexcept;
    };
}
