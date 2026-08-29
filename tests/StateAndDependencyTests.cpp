#include "UHI/HotkeyCategory.h"
#include "UHI/ActivationContextInference.h"
#include "UHI/LastScanStore.h"
#include "UHI/SexLabDependency.h"
#include "UHI/StorageUtilOwnerResolver.h"
#include "UHI/SkyrimRuntimeLayout.h"

#include <array>
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string_view>
#include <vector>

namespace
{
    template <class T>
    void Append(std::vector<char>& bytes, const T& value)
    {
        const auto* first = reinterpret_cast<const char*>(&value);
        bytes.insert(bytes.end(), first, first + sizeof(value));
    }

    void AppendSubrecord(std::vector<char>& data, const std::string_view type,
        const std::string_view value)
    {
        data.insert(data.end(), type.begin(), type.end());
        const auto size = static_cast<std::uint16_t>(value.size() + 1);
        Append(data, size);
        data.insert(data.end(), value.begin(), value.end());
        data.push_back('\0');
    }

    bool WritePlugin(const std::filesystem::path& path, const std::string_view master = {})
    {
        std::vector<char> data;
        if (!master.empty()) AppendSubrecord(data, "MAST", master);
        std::array<char, 24> header{};
        std::memcpy(header.data(), "TES4", 4);
        const auto size = static_cast<std::uint32_t>(data.size());
        std::memcpy(header.data() + 4, &size, sizeof(size));
        std::ofstream output(path, std::ios::binary);
        output.write(header.data(), static_cast<std::streamsize>(header.size()));
        output.write(data.data(), static_cast<std::streamsize>(data.size()));
        return static_cast<bool>(output);
    }
}

int main()
{
    const auto fail = [](const char* stage) {
        std::cerr << "State/dependency test failed at: " << stage << '\n';
        return 1;
    };
    const auto root = std::filesystem::temp_directory_path() / "uhm_state_dependency_test";
    const auto seLayout = UHI::SkyrimInputContextLayoutForVersion(1, 5, 97);
    const auto ae640Layout = UHI::SkyrimInputContextLayoutForVersion(1, 6, 640);
    const auto ae1129Layout = UHI::SkyrimInputContextLayoutForVersion(1, 6, 1129);
    const auto ae1130Layout = UHI::SkyrimInputContextLayoutForVersion(1, 6, 1130);
    const auto ae1170Layout = UHI::SkyrimInputContextLayoutForVersion(1, 6, 1170);
    const auto seRenderer = UHI::NativeRendererHookLayoutForVersion(1, 5, 97, 0);
    const auto aeRenderer = UHI::NativeRendererHookLayoutForVersion(1, 6, 1170, 0);
    if (!seLayout || seLayout->runtimeContextCount != 17 || seLayout->hasMarketplaceContext ||
        seLayout->CanonicalContextIndex(16) != UHI::kCanonicalFavorContextIndex ||
        !ae640Layout || ae640Layout->runtimeContextCount != 17 || ae640Layout->hasMarketplaceContext ||
        ae640Layout->CanonicalContextIndex(16) != UHI::kCanonicalFavorContextIndex ||
        !ae1129Layout || ae1129Layout->runtimeContextCount != 17 || ae1129Layout->hasMarketplaceContext ||
        !ae1130Layout || ae1130Layout->runtimeContextCount != 18 || !ae1130Layout->hasMarketplaceContext ||
        ae1130Layout->CanonicalContextIndex(16) != UHI::kCanonicalMarketplaceContextIndex ||
        ae1130Layout->CanonicalContextIndex(17) != UHI::kCanonicalFavorContextIndex ||
        !ae1170Layout || ae1170Layout->runtimeContextCount != 18 || !ae1170Layout->hasMarketplaceContext ||
        UHI::SkyrimInputContextLayoutForVersion(1, 7, 0).has_value()) {
        return fail("SE/AE input-context layout boundary");
    }
    if (!seRenderer || seRenderer->d3dInitRelocationID != 75595 ||
        seRenderer->d3dInitCallOffset != 0x50 ||
        !aeRenderer || aeRenderer->d3dInitRelocationID != 77226 ||
        aeRenderer->d3dInitCallOffset != 0x2BC ||
        UHI::NativeRendererHookLayoutForVersion(1, 6, 1171, 0).has_value() ||
        UHI::NativeRendererHookLayoutForVersion(1, 6, 1170, 1).has_value()) {
        return fail("SE/AE native-renderer hook layout boundary");
    }
    std::error_code error;
    std::filesystem::remove_all(root, error);
    std::filesystem::create_directories(root / "Data" / "MCM" / "Settings", error);
    if (error || !WritePlugin(root / "Data" / "SexLab.esm") ||
        !WritePlugin(root / "Data" / "DefeatSystem.esp", "SexLab.esm") ||
        !WritePlugin(root / "Data" / "DefeatPatch.esl", "DefeatSystem.esp")) return fail("plugin fixtures");

    const auto settings = root / "Data" / "MCM" / "Settings" / "DefeatSystem.ini";
    {
        std::ofstream output(settings);
        output << "ResistKey=42\n";
    }
    std::vector records{ UHI::HotkeyRecord{
        .owner = "DefeatSystem", .action = "Resist", .binding = "LShift", .rawBinding = "42",
        .codeSystem = "SKSE unified input code", .device = "keyboard", .detector = "test",
        .confidence = UHI::Confidence::inferred, .evidencePath = settings, .evidenceLine = 1,
        .stage = UHI::ScanStage::configuration, .editable = true, .conflictEligible = true
    } };
    const auto dependencies = UHI::DiscoverSexLabDependencies(root);
    if (!dependencies.installed || !dependencies.pluginFiles.contains("defeatsystem.esp") ||
        !dependencies.pluginFiles.contains("defeatpatch.esl")) return fail("dependency discovery");
    UHI::ApplySexLabDependencyHints(dependencies, records);
    if (records.front().categoryHint != "sexlab" ||
        UHI::ClassifyHotkey(records.front()) != UHI::HotkeyCategory::sexlab) return fail("dependency category hint");
    std::vector ecosystemRecords{
        UHI::HotkeyRecord{ .owner = "sslConfigMenu", .action = "Advance", .binding = "Space",
            .evidencePath = root / "Data" / "Scripts" / "sslConfigMenu.pex" },
        UHI::HotkeyRecord{ .owner = "SLATE", .action = "Open", .binding = "F8" },
        UHI::HotkeyRecord{ .owner = "SexLab Utility Plus", .action = "Adjust", .binding = "L" },
        UHI::HotkeyRecord{ .owner = "SLAnimLoader", .action = "Register", .binding = "K" },
        UHI::HotkeyRecord{ .owner = "SLSO", .action = "Scene control", .binding = "O" },
        UHI::HotkeyRecord{ .owner = "DeviousDevices", .action = "Device menu", .binding = "D" }
    };
    UHI::ApplySexLabDependencyHints(dependencies, ecosystemRecords);
    if (std::ranges::any_of(ecosystemRecords,
        [](const auto& record) { return record.categoryHint != "sexlab"; })) return 1;
    const auto sceneContext = static_cast<std::uint32_t>(UHI::ActivationContext::sexLabScene);
    if (ecosystemRecords[2].contextMask != sceneContext || ecosystemRecords[4].contextMask != sceneContext ||
        ecosystemRecords[2].contextConfidence != UHI::ContextConfidence::confirmed ||
        ecosystemRecords[4].contextConfidence != UHI::ContextConfidence::confirmed ||
        ecosystemRecords[1].contextMask != 0 || ecosystemRecords[3].contextMask != 0) return fail("SexLab scene contexts");
    auto gameControl = records.front();
    gameControl.owner = "Skyrim default controls";
    gameControl.action = "Attack";
    gameControl.detector = "ControlMapScanner";
    if (UHI::ClassifyHotkey(gameControl) != UHI::HotkeyCategory::game) return fail("game category");

    const auto category = [](std::string owner, std::string action) {
        UHI::HotkeyRecord record{ .owner = std::move(owner), .action = std::move(action), .binding = "F8" };
        return UHI::ClassifyHotkey(record);
    };
    if (category("DualWieldParryingNG", "Parry") != UHI::HotkeyCategory::combat ||
        category("TrueDirectionalMovement", "Open Target Lock Menu") != UHI::HotkeyCategory::combat ||
        category("OCPA", "Hotkey") != UHI::HotkeyCategory::combat ||
        category("Equipment Manager", "Open Outfit Menu") != UHI::HotkeyCategory::character ||
        category("Follower Framework", "Open Follower Menu") != UHI::HotkeyCategory::character ||
        category("SmoothCam", "Toggle Camera") != UHI::HotkeyCategory::environment ||
        category("SkyUI", "Open inventory") != UHI::HotkeyCategory::user_interface ||
        category("OBody", "Open body preset menu") != UHI::HotkeyCategory::character ||
        category("SOS", "Bend") != UHI::HotkeyCategory::character ||
        category("Private Needs - Orgasm", "Check needs") != UHI::HotkeyCategory::sexlab ||
        category("DD", "Device menu") != UHI::HotkeyCategory::sexlab ||
        category("HUD Widgets", "Open Settings Menu") != UHI::HotkeyCategory::user_interface ||
        category("Community Shaders", "Next blocked shader") != UHI::HotkeyCategory::external) return fail("purpose categories");

    const auto menuMask = static_cast<std::uint32_t>(UHI::ActivationContext::menuMode);
    if (!UHI::IsUiLocalInput("Custom Menu", "Next tab", "PexBytecodeScanner", menuMask) ||
        UHI::IsUiLocalInput("Custom Menu", "Open menu", "PexBytecodeScanner", menuMask) ||
        !UHI::IsUiLocalInput("SkyUI_SE", "i Search", "StructuredConfigScanner", 0) ||
        !UHI::IsUiLocalInput("PhotoMode", "Take Photo", "StructuredConfigScanner", 0) ||
        UHI::IsUiLocalInput("Modex", "Open Menu", "StructuredConfigScanner", 0) ||
        UHI::IsUiLocalInput("Skyrim default controls", "Next tab", "ControlMapScanner", menuMask)) return fail("UI-local inputs");

    const auto storageFile = root / "Data" / "SKSE" / "Plugins" /
        "StorageUtilData" / "DefeatSystem" / "Settings.json";
    std::filesystem::create_directories(storageFile.parent_path(), error);
    records.push_back({
        .owner = "StorageUtilData", .action = "Resist", .binding = "F8",
        .evidencePath = storageFile, .stage = UHI::ScanStage::configuration
    });
    UHI::ResolveStorageUtilOwners(root, records);
    if (records.back().owner != "DefeatSystem") return fail("StorageUtil owner");
    records.pop_back();

    const auto snapshot = root / "state" / "last-scan.bin";
    if (!UHI::LastScanStore{}.Save(snapshot, records)) return fail("last scan save");
    const auto restored = UHI::LastScanStore{}.Load(snapshot);
    if (!restored || restored->size() != 1 || restored->front().binding != "LShift" ||
        restored->front().categoryHint != "sexlab" || restored->front().evidencePath != settings) return fail("last scan restore");
    {
        std::ofstream output(settings, std::ios::app);
        output << "Changed=true\n";
    }
    if (UHI::LastScanStore{}.Load(snapshot)) return fail("last scan invalidation");
    const auto comparisonBaseline = UHI::LastScanStore{}.Load(snapshot, false);
    if (!comparisonBaseline || comparisonBaseline->size() != 1 ||
        comparisonBaseline->front().binding != "LShift") return fail("stale comparison baseline");

    std::filesystem::remove_all(root, error);
    std::cout << "Last-scan restore and SexLab dependency tests passed\n";
    return 0;
}
