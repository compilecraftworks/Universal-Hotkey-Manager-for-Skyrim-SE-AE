#include "UHI/GameFilePolicy.h"
#include "UHI/ScanCache.h"

#include <filesystem>
#include <fstream>
#include <iostream>

int main()
{
    const auto root = std::filesystem::temp_directory_path() / "uhi_scan_cache_test";
    std::error_code error;
    std::filesystem::remove_all(root, error);
    std::filesystem::create_directories(root);
    const auto source = root / "Settings.ini";
    const auto cachePath = root / "cache.bin";
    {
        std::ofstream output(source);
        output << "Hotkey=F10\n";
    }
    UHI::HotkeyRecord record{
        .owner = "Cache test", .action = "Open", .binding = "F10", .rawBinding = "F10",
        .codeSystem = "symbolic", .device = "keyboard", .detector = "test",
        .confidence = UHI::Confidence::confirmed, .evidencePath = source, .evidenceLine = 1,
        .stage = UHI::ScanStage::configuration, .editable = true, .conflictEligible = true,
        .uiLocalOnly = true
    };
    {
        UHI::ScanCache cache(cachePath);
        cache.Load();
        if (cache.Find("config-v-test", source)) return 1;
        cache.Store("config-v-test", source, std::vector{ record });
        cache.Save();
    }
    {
        UHI::ScanCache cache(cachePath);
        cache.Load();
        const auto found = cache.Find("config-v-test", source);
        if (!found || found->size() != 1 || found->front().binding != "F10" ||
            found->front().evidencePath != source || !found->front().uiLocalOnly || cache.HitCount() != 1) return 1;
        cache.Save();
    }
    {
        std::ofstream output(source, std::ios::app);
        output << "Changed=true\n";
    }
    {
        UHI::ScanCache cache(cachePath);
        cache.Load();
        if (cache.Find("config-v-test", source) || cache.MissCount() != 1) return 1;
    }

    const auto loose = root / "Data" / "Interface" / "Controls" / "PC" / "controlmap.txt";
    const bool policyOk = UHI::IsLooseControlMap(loose) && !UHI::IsKnownBethesdaGameFile(loose) &&
        UHI::IsKnownBethesdaGameFile(root / "ControlMap_Custom.txt") &&
        UHI::IsKnownBethesdaGameFile(root / "Skyrim - Interface.bsa") &&
        UHI::IsKnownBethesdaGameFile(root / "Dawnguard.bsa") &&
        UHI::IsKnownBethesdaGameFile(root / "ccBGSSSE001-Fish.bsa") &&
        UHI::IsKnownBethesdaGameFile(root / "MarketplaceTextures.bsa") &&
        UHI::IsKnownBethesdaGameFile(root / "_ResourcePack.bsa") &&
        !UHI::IsKnownBethesdaGameFile(source);
    std::filesystem::remove_all(root, error);
    if (!policyOk) {
        std::cerr << "Game file policy test failed\n";
        return 1;
    }
    std::cout << "Scan cache and game file policy test passed\n";
    return 0;
}
