#include "UHI/ScanPipeline.h"

#include "UHI/scanners/GenericConfigScanner.h"
#include "UHI/scanners/ReShadeScanner.h"
#include "UHI/scanners/CommunityShadersScanner.h"
#include "UHI/scanners/EnbScanner.h"
#include "UHI/scanners/PexScanner.h"
#include "UHI/scanners/DllScanner.h"
#include "UHI/scanners/ControlMapScanner.h"
#include "UHI/scanners/BsaScanner.h"
#include "UHI/scanners/UserDataScanner.h"
#include "UHI/ScanCache.h"
#include "UHI/OpeningHotkey.h"
#include "UHI/ActivationContextInference.h"
#include "UHI/SexLabDependency.h"
#include "UHI/StorageUtilOwnerResolver.h"

#include <algorithm>
#include <array>

namespace
{
    const char* StageName(const UHI::ScanStage stage)
    {
        switch (stage) {
        case UHI::ScanStage::configuration: return "Configuration & archives";
        case UHI::ScanStage::scripts: return "Scripts";
        case UHI::ScanStage::nativePlugins: return "Native plugins";
        case UHI::ScanStage::runtime: return "Saved & runtime registrations";
        }
        return "Unknown";
    }
}

namespace UHI
{
    std::vector<HotkeyRecord> ScanPipeline::Run(const std::filesystem::path& gameRoot,
        ProgressCallback progress, const CancelCallback& cancel,
        const std::string_view preferredSaveName) const
    {
        std::vector<HotkeyRecord> records;
        const auto cachePath = gameRoot / "Data" / "SKSE" / "Plugins" /
            "UniversalHotkeyManager" / "scan-cache-v1.bin";
        ScanCache cache(cachePath);
        cache.Load();
        bool collectionLimitReached = false;
        std::vector<HotkeyRecord> archiveScriptRecords;
        SexLabDependencyIndex sexLabDependencies;
        const auto append = [&](std::vector<HotkeyRecord>& found) {
            collectionLimitReached = AppendScanResults(records, found) || collectionLimitReached;
        };
        constexpr std::array stages{
            ScanStage::configuration, ScanStage::scripts,
            ScanStage::nativePlugins, ScanStage::runtime
        };

        for (std::size_t index = 0; index < stages.size(); ++index) {
            if (ScanCancelled(cancel) || collectionLimitReached) break;
            const auto stage = stages[index];
            const auto report = [&](std::size_t completed, std::size_t total,
                                    const std::filesystem::path& current = {},
                                    const float currentFilePercent = 100.0F) {
                if (!progress) {
                    return;
                }
                const auto fraction = total == 0 ? 1.0F : static_cast<float>(completed) / static_cast<float>(total);
                progress({ .stage = stage, .stageName = StageName(stage), .currentPath = current,
                    .stageIndex = index, .stageCount = stages.size(),
                    .completedItems = completed, .totalItems = total,
                    .overallPercent = (static_cast<float>(index) + fraction) * 100.0F /
                        static_cast<float>(stages.size()),
                    .currentFilePercent = std::clamp(currentFilePercent, 0.0F, 100.0F) });
            };

            if (progress) {
                progress({ .stage = stage, .stageName = StageName(stage), .stageIndex = index, .stageCount = stages.size(),
                    .overallPercent = static_cast<float>(index) * 100.0F / static_cast<float>(stages.size()) });
            }

            if (stage == ScanStage::configuration) {
                // Five specialized sources, up to twelve targeted config
                // roots, and one mod-archive pass.
                constexpr std::size_t total = 19;
                std::size_t completed{};
                const auto openingHotkeyPath = gameRoot / "Data" / "SKSE" / "Plugins" /
                    "UniversalHotkeyManager.ini";
                const auto openingHotkey = LoadOpeningHotkey(openingHotkeyPath);
                records.push_back({
                    .owner = "Universal Hotkey Manager for Skyrim SE-AE",
                    .action = "Open shortcut manager",
                    .binding = FormatOpeningHotkey(openingHotkey),
                    .rawBinding = "DirectInput scan code " + std::to_string(openingHotkey.scanCode),
                    .codeSystem = "UHI configurable DirectInput chord",
                    .device = "keyboard",
                    .detector = "BuiltInHotkeyTable",
                    .confidence = Confidence::confirmed,
                    .evidencePath = openingHotkeyPath,
                    .stage = ScanStage::configuration,
                    .editable = true,
                    .conflictEligible = true
                });
                report(++completed, total, openingHotkeyPath, 100.0F);
                Scanners::ReShadeScanner reshade;
                const auto reshadePath = gameRoot / "ReShade.ini";
                report(completed, total, reshadePath, 0.0F);
                auto reshadeRecords = reshade.Scan(reshadePath);
                append(reshadeRecords);
                report(++completed, total, reshadePath, 100.0F);

                Scanners::EnbScanner enb;
                const auto enbPath = gameRoot / "enblocal.ini";
                report(completed, total, enbPath, 0.0F);
                auto enbRecords = enb.Scan(enbPath);
                append(enbRecords);
                report(++completed, total, enbPath, 100.0F);

                Scanners::GenericConfigScanner generic;
                Scanners::ControlMapScanner controlMap;
                const auto controlPath = gameRoot / "Data" / "Interface" / "Controls" / "PC" / "controlmap.txt";
                report(completed, total, controlPath, 0.0F);
                auto controlRecords = std::filesystem::exists(controlPath) ? controlMap.Scan(controlPath) :
                    controlMap.KnownGameDefaults(gameRoot);
                append(controlRecords);
                report(++completed, total, controlPath, 100.0F);

                Scanners::CommunityShadersScanner communityShaders;
                auto communityPath = gameRoot / "Data" / "SKSE" / "Plugins" / "CommunityShaders" / "SettingsUser.json";
                if (!std::filesystem::exists(communityPath)) {
                    communityPath = gameRoot / "Data" / "SKSE" / "Plugins" / "CommunityShaders" / "SettingsDefault.json";
                }
                report(completed, total, communityPath, 0.0F);
                auto communityRecords = communityShaders.Scan(communityPath);
                append(communityRecords);
                report(++completed, total, communityPath, 100.0F);
                // Scan configuration-bearing virtual roots, not millions of
                // meshes/textures/animations. The game and Data roots are
                // shallow; known config subtrees remain recursive.
                struct ConfigRoot { std::filesystem::path path; bool recursive; NumericCodeSpace fallback; };
                const std::array roots{
                    ConfigRoot{ gameRoot, false, NumericCodeSpace::windowsVirtualKey },
                    // Native plugins are free to load live settings from any
                    // subdirectory below Data. Scan the active VFS tree once
                    // instead of maintaining a lossy list of known layouts.
                    ConfigRoot{ gameRoot / "Data", true, NumericCodeSpace::skseUnifiedInputCode },
                    ConfigRoot{ gameRoot / "SKSE", true, NumericCodeSpace::skseUnifiedInputCode },
                    ConfigRoot{ gameRoot / "MCM", true, NumericCodeSpace::skseUnifiedInputCode },
                    ConfigRoot{ gameRoot / "configs", true, NumericCodeSpace::skseUnifiedInputCode }
                };
                for (const auto& configRoot : roots) {
                    if (ScanCancelled(cancel) || collectionLimitReached) break;
                    const auto& root = configRoot.path;
                    if (!std::filesystem::exists(root)) continue;
                    const auto itemProgress = [&](const std::filesystem::path& current,
                        const std::size_t done, const std::size_t count, const float filePercent) {
                        constexpr std::size_t scale = 1000;
                        const auto within = count == 0 ? scale : done * scale / count;
                        report(completed * scale + within, total * scale, current, filePercent);
                    };
                    auto configRecords = generic.Scan(root, configRoot.recursive, cancel,
                        {}, configRoot.fallback, &cache, 4, itemProgress);
                    append(configRecords);
                    report(++completed, total, root);
                }
                Scanners::BsaScanner archiveScanner;
                std::vector<HotkeyRecord> archiveRecords;
                if (!collectionLimitReached) {
                    const auto itemProgress = [&](const std::filesystem::path& current,
                        const std::size_t done, const std::size_t count, const float filePercent) {
                        constexpr std::size_t scale = 1000;
                        const auto within = count == 0 ? scale : done * scale / count;
                        report(completed * scale + within, total * scale, current, filePercent);
                    };
                    archiveRecords = archiveScanner.ScanAll(gameRoot / "Data", cancel,
                        {}, &cache, 4, itemProgress);
                    std::vector<HotkeyRecord> archiveConfigRecords;
                    archiveConfigRecords.reserve(archiveRecords.size());
                    archiveScriptRecords.reserve(archiveRecords.size());
                    for (auto& record : archiveRecords) {
                        if (record.stage == ScanStage::scripts) archiveScriptRecords.push_back(std::move(record));
                        else archiveConfigRecords.push_back(std::move(record));
                    }
                    append(archiveConfigRecords);
                }
                report(++completed, total, gameRoot / "Data");
                if (!ScanCancelled(cancel)) {
                    const auto dependencyProgress = [&](const std::filesystem::path& current,
                        const std::size_t done, const std::size_t count) {
                        constexpr std::size_t scale = 1000;
                        const auto within = count == 0 ? scale : done * scale / count;
                        report(completed * scale + within, total * scale, current, 0.0F);
                    };
                    sexLabDependencies = DiscoverSexLabDependencies(gameRoot, dependencyProgress, cancel);
                }
                report(++completed, total, gameRoot / "Data" / "SexLab.esm");
            }

            if (stage == ScanStage::scripts) {
                constexpr std::size_t total = 2;
                std::size_t completed{};
                Scanners::PexScanner scanner;
                const auto scriptProgress = [&](const std::filesystem::path& current,
                    const std::size_t done, const std::size_t count, const float filePercent) {
                    constexpr std::size_t scale = 1000;
                    const auto within = count == 0 ? scale : done * scale / count;
                    report(completed * scale + within, total * scale, current, filePercent);
                };
                auto scriptRecords = scanner.Scan(gameRoot / "Data" / "Scripts", cancel,
                    {}, &cache, 4, scriptProgress);
                append(scriptRecords);
                report(++completed, total, gameRoot / "Data");
                if (!collectionLimitReached) append(archiveScriptRecords);
                report(++completed, total, gameRoot / "Data");
            }

            if (stage == ScanStage::nativePlugins) {
                Scanners::DllScanner scanner;
                auto dllRecords = scanner.Scan(gameRoot / "Data" / "SKSE" / "Plugins", cancel,
                    {}, &cache, 4,
                    [&](const std::filesystem::path& current, const std::size_t done,
                        const std::size_t count, const float filePercent) {
                        report(done, count, current, filePercent);
                    });
                append(dllRecords);
                report(1, 1, gameRoot / "Data" / "SKSE" / "Plugins");
            }

            if (stage == ScanStage::runtime) {
                Scanners::UserDataScanner scanner;
                auto savedRecords = scanner.Scan(gameRoot, preferredSaveName, cancel, {},
                    [&](const auto& path) { report(0, 1, path, 0.0F); });
                append(savedRecords);
                report(1, 1, preferredSaveName.empty() ? std::filesystem::path("Saved user data") :
                    std::filesystem::path(preferredSaveName));
            }

            if (progress && !ScanCancelled(cancel)) {
                progress({ .stage = stage, .stageName = StageName(stage), .stageIndex = index,
                    .stageCount = stages.size(), .completedItems = 1, .totalItems = 1,
                    .overallPercent = static_cast<float>(index + 1) * 100.0F / static_cast<float>(stages.size()) });
            }
        }

        if (!ScanCancelled(cancel)) {
            ResolveStorageUtilOwners(gameRoot, records);
            for (auto& record : records) {
                NormalizeRecordDisplayNames(record);
                // Static code scanners prove that a key constant exists, not
                // that the corresponding script instance or native handler is
                // active in the current game. Keep that evidence in the raw
                // report, but do not present it as a loaded binding.
                if (record.detector == "PexScanner" || record.detector == "PexBytecodeScanner" ||
                    record.detector == "DllScanner" || record.detector == "DllDisassemblyScanner" ||
                    record.detector == "UserDataScanner") {
                    record.runtimeActive = false;
                    record.editable = false;
                }
                // Deep CommonLib disassembly is useful forensic evidence, but
                // it is not proof that the handler is registered in this game.
                // Only ActiveInputSinkAnalyzer records captured from the live
                // sink list may enter the ordinary hotkey views/conflicts.
                if (record.detector == "StaticCommonLibInputHandler") {
                    record.runtimeActive = false;
                    record.editable = false;
                    record.conflictEligible = false;
                }
                record.uiLocalOnly = IsUiLocalInput(record.owner, record.action,
                    record.detector, record.contextMask);
            }
            ApplySexLabDependencyHints(sexLabDependencies, records);
            cache.Save();
        }
        if (progress && !ScanCancelled(cancel)) {
            progress({ .stage = stages.back(), .stageName = StageName(stages.back()),
                .stageIndex = stages.size() - 1, .stageCount = stages.size(),
                .completedItems = 1, .totalItems = 1, .overallPercent = 100.0F, .finished = true });
        }
        return records;
    }
}
