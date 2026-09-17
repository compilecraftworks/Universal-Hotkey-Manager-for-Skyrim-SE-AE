#pragma once

#include "UHI/HotkeyRecord.h"
#include "UHI/ScanControl.h"

#include <filesystem>
#include <functional>
#include <string_view>
#include <vector>

namespace UHI
{
    struct ScanProgress
    {
        ScanStage stage{ ScanStage::configuration };
        const char* stageName{ "Configuration files" };
        std::filesystem::path currentPath;
        std::size_t stageIndex{};
        std::size_t stageCount{ 4 };
        std::size_t completedItems{};
        std::size_t totalItems{};
        float overallPercent{};
        float currentFilePercent{};
        bool finished{};
    };

    // Re-evaluate loaded-plugin evidence after cache restoration as well as
    // after a scan. Loose settings can survive disabling a mod in MO2.
    void ApplyLoadedPluginState(std::vector<HotkeyRecord>& records,
        const std::function<bool(std::string_view)>& isLoaded);

    class ScanPipeline
    {
    public:
        using ProgressCallback = std::function<void(const ScanProgress&)>;

        [[nodiscard]] std::vector<HotkeyRecord> Run(
            const std::filesystem::path& gameRoot,
            ProgressCallback progress = {},
            const CancelCallback& cancel = {},
            std::string_view preferredSaveName = {}) const;
    };
}
