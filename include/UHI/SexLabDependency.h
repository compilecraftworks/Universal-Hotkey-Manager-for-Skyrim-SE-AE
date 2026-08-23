#pragma once

#include "UHI/HotkeyRecord.h"
#include "UHI/ScanControl.h"

#include <filesystem>
#include <functional>
#include <span>
#include <string>
#include <unordered_set>

namespace UHI
{
    struct SexLabDependencyIndex
    {
        bool installed{};
        std::unordered_set<std::string> pluginFiles;
        std::unordered_set<std::string> identityTokens;
    };

    using DependencyProgressCallback = std::function<void(
        const std::filesystem::path&, std::size_t, std::size_t)>;

    [[nodiscard]] bool IsSexLabInstalled(const std::filesystem::path& gameRoot) noexcept;
    [[nodiscard]] SexLabDependencyIndex DiscoverSexLabDependencies(
        const std::filesystem::path& gameRoot,
        const DependencyProgressCallback& progress = {},
        const CancelCallback& cancel = {}) noexcept;
    void ApplySexLabDependencyHints(const SexLabDependencyIndex& index,
        std::span<HotkeyRecord> records) noexcept;
}
