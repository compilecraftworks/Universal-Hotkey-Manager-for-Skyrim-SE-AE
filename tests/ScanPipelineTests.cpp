#include "UHI/ScanPipeline.h"

#include <algorithm>
#include <filesystem>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string_view>
#include <vector>

int main()
{
    const auto root = std::filesystem::temp_directory_path() / "uhi_pipeline_test";
    // A prior interrupted test may leave its persistent scan cache behind.
    // Start from a deterministic fixture instead of inheriting stale records.
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root);
    {
        std::ofstream output(root / "ReShade.ini");
        output << "KeyOverlay=36,0,0,0\n";
    }
    std::filesystem::create_directories(root / "Data" / "SKSE" / "Plugins");
    std::filesystem::create_directories(root / "SKSE" / "Plugins");
    std::filesystem::create_directories(root / "MCM");
    {
        std::ofstream output(root / "RootConfig.ini");
        output << "HotkeyRoot=F6\n";
    }
    {
        std::ofstream output(root / "Data" / "example.ini");
        output << "HotkeyData=F7\n";
    }
    {
        std::ofstream output(root / "SKSE" / "Plugins" / "example.ini");
        output << "HotkeySkse=F8\n";
    }
    {
        std::ofstream output(root / "MCM" / "example.ini");
        output << "HotkeyMcm=F9\n";
    }

    std::vector<UHI::ScanStage> stages;
    bool finished = false;
    bool sawPath = false;
    bool sawFileProgress = false;
    float previousPercent = 0.0F;
    const auto records = UHI::ScanPipeline{}.Run(root, [&](const UHI::ScanProgress& progress) {
        if (stages.empty() || stages.back() != progress.stage) {
            stages.push_back(progress.stage);
        }
        finished = finished || progress.finished;
        sawPath = sawPath || !progress.currentPath.empty();
        if (progress.currentFilePercent < 0.0F || progress.currentFilePercent > 100.0F) {
            std::cerr << "Current-file progress out of range\n";
            std::exit(1);
        }
        sawFileProgress = sawFileProgress ||
            (!progress.currentPath.empty() && progress.currentFilePercent > 0.0F);
        if (progress.overallPercent + 0.001F < previousPercent) {
            std::cerr << "Scan progress regressed\n";
            std::exit(1);
        }
        previousPercent = progress.overallPercent;
    });
    std::filesystem::remove_all(root);

    const auto has = [&](const std::string_view owner, const std::string_view action,
                         const std::string_view binding) {
        return std::ranges::any_of(records, [&](const auto& record) {
            return record.owner == owner && record.action == action && record.binding == binding;
        });
    };
    // The four generic files, ReShade, and the compiled-in Skyrim defaults are
    // collected. A recursive game-root scan must not duplicate Data/SKSE/MCM.
    if (records.size() < 55 || !has("Skyrim Controls", "Forward", "W") ||
        !has("Skyrim Controls", "Activate", "E") ||
        !has("Skyrim Controls", "Activate", "A") ||
        !has("Universal Hotkey Manager for Skyrim SE-AE", "Open shortcut manager", "Del") || stages.size() != 4 ||
        stages[0] != UHI::ScanStage::configuration ||
        stages[1] != UHI::ScanStage::scripts ||
        stages[2] != UHI::ScanStage::nativePlugins ||
        stages[3] != UHI::ScanStage::runtime || !finished || !sawPath || !sawFileProgress ||
        previousPercent < 100.0F) {
        std::cerr << "Scan pipeline test failed: records=" << records.size()
                  << " stages=" << stages.size() << " finished=" << finished
                  << " sawPath=" << sawPath << " sawFileProgress=" << sawFileProgress
                  << " percent=" << previousPercent << "\n";
        for (const auto& record : records) {
            if (record.binding == "Del" || record.binding == "W" || record.binding == "E" ||
                record.binding == "A") {
                std::cerr << "  " << record.owner << " | " << record.action << " | "
                          << record.binding << " | " << record.detector << "\n";
            }
        }
        return 1;
    }
    std::cout << "Scan pipeline test passed\n";
    return 0;
}
