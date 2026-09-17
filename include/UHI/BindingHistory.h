#pragma once
#include "UHI/HotkeyRecord.h"
#include <optional>
#include <vector>

namespace UHI
{
    struct BindingChange { HotkeyRecord before; HotkeyRecord after; };
    [[nodiscard]] std::vector<BindingChange> LoadBindingHistory(const std::filesystem::path& path);
    [[nodiscard]] bool SaveBindingChange(const std::filesystem::path& path,
        const HotkeyRecord& before, std::string_view newRaw);
    [[nodiscard]] bool SameBindingSource(const HotkeyRecord& left, const HotkeyRecord& right);
    [[nodiscard]] std::optional<std::string> OriginalBackupValue(const HotkeyRecord& record);
}
