#include "UHI/JsonReporter.h"
#include "UHI/Registry.h"
#include "UHI/ScanPipeline.h"
#include "UHI/MenuFrameworkAdapter.h"
#include "UHI/OpeningHotkey.h"
#include "UHI/LastScanStore.h"
#include "UHI/SexLabDependency.h"
#include "UHI/PathEncoding.h"
#include "UHI/ConfigBindingParser.h"
#include "UHI/BindingSerializer.h"
#include "UHI/scanners/ControlMapScanner.h"
#include "UHI/scanners/PeInputAnalyzer.h"
#include "UHI/ActivationContextInference.h"
#include "UHI/writers/ControlMapWriter.h"
#include "UHI/writers/ConfigFileWriter.h"

#include <SKSE/SKSE.h>
#include <SKSE/InputMap.h>
#include <RE/N/NativeFunction.h>
#include <RE/Skyrim.h>
#include <RE/B/BSInputDeviceManager.h>

#include <filesystem>
#include <algorithm>
#include <array>
#include <memory>
#include <thread>
#include <atomic>
#include <mutex>
#include <chrono>
#include <cctype>
#include <cmath>
#include <charconv>
#include <optional>
#include <unordered_set>
#include <unordered_map>
#ifdef _WIN32
#include <Windows.h>
#ifdef GetObject
#undef GetObject
#endif
#endif

#include <spdlog/sinks/basic_file_sink.h>

#ifndef UHI_VERSION
#define UHI_VERSION "0.0.0"
#endif

namespace
{
    std::atomic_bool g_scanRunning{ false };
    std::atomic_bool g_scanCancelRequested{ false };
    // Live Papyrus/VM objects are invalidated while a game is reverting or a
    // save is being loaded.  UHM must never enumerate them during that window.
    std::atomic_bool g_gameTransitioning{ false };
    std::atomic<float> g_scanPercent{ 0.0F };
    std::atomic<float> g_scanFilePercent{ 0.0F };
    std::string g_scanStage = "Waiting";
    std::mutex g_scanStageMutex;
    std::string g_scanPath;
    std::shared_ptr<const UHI::Registry> g_lastRegistry = std::make_shared<UHI::Registry>();
    std::shared_ptr<const UHI::Registry> g_lastComparisonRegistry;
    std::size_t g_lastConflictCount{};
    std::mutex g_registryMutex;
    std::string g_currentSaveName;
    std::mutex g_currentSaveMutex;
    std::atomic_uint32_t g_openingHotkeyPacked{ 0xD3 };
    std::atomic<float> g_uiScale{ 1.0F };
    std::atomic<float> g_windowOpacity{ 0.90F };
    std::atomic<std::uint8_t> g_uiLanguage{
        static_cast<std::uint8_t>(UHI::UiLanguage::automatic) };
    std::atomic_bool g_openingHotkeyPollStarted{ false };
    std::atomic_bool g_uhmPauseOwned{ false };
    std::atomic_int64_t g_lastOpeningToggleMilliseconds{ -1000 };
    std::atomic_bool g_hasValidatedScanSnapshot{ false };

    constexpr std::uint32_t EncodeModifierSide(const std::uint32_t code,
        const std::uint32_t left, const std::uint32_t right) noexcept
    {
        return code == left ? 1U : code == right ? 2U : 0U;
    }

    constexpr std::uint32_t DecodeModifierSide(const std::uint32_t value,
        const std::uint32_t left, const std::uint32_t right) noexcept
    {
        return value == 1U ? left : value == 2U ? right : 0U;
    }

    constexpr std::uint32_t PackOpeningHotkey(const UHI::OpeningHotkey& hotkey) noexcept
    {
        return (hotkey.scanCode & 0xFFU) | (hotkey.ctrl ? 1U << 8U : 0U) |
            (hotkey.shift ? 1U << 9U : 0U) | (hotkey.alt ? 1U << 10U : 0U) |
            (EncodeModifierSide(hotkey.ctrlScanCode, 0x1D, 0x9D) << 11U) |
            (EncodeModifierSide(hotkey.shiftScanCode, 0x2A, 0x36) << 13U) |
            (EncodeModifierSide(hotkey.altScanCode, 0x38, 0xB8) << 15U);
    }

    constexpr UHI::OpeningHotkey UnpackOpeningHotkey(const std::uint32_t packed) noexcept
    {
        return { .scanCode = packed & 0xFFU, .ctrl = (packed & (1U << 8U)) != 0,
            .shift = (packed & (1U << 9U)) != 0, .alt = (packed & (1U << 10U)) != 0,
            .ctrlScanCode = DecodeModifierSide((packed >> 11U) & 0x3U, 0x1D, 0x9D),
            .shiftScanCode = DecodeModifierSide((packed >> 13U) & 0x3U, 0x2A, 0x36),
            .altScanCode = DecodeModifierSide((packed >> 15U) & 0x3U, 0x38, 0xB8) };
    }

    std::filesystem::path OpeningHotkeyPath()
    {
        return std::filesystem::current_path() / "Data" / "SKSE" / "Plugins" /
            "UniversalHotkeyManager.ini";
    }

    std::filesystem::path LastScanPath()
    {
        return std::filesystem::current_path() / "Data" / "SKSE" / "Plugins" /
            "UniversalHotkeyManager" / "last-scan-v1.bin";
    }

    std::string ScanDisplayPath(const std::filesystem::path& path,
        const std::filesystem::path& gameRoot)
    {
        if (path.empty()) return {};
        const auto relative = path.lexically_relative(gameRoot);
        const auto first = relative.begin();
        if (!relative.empty() && first != relative.end() &&
            *first != std::filesystem::path("..")) {
            return UHI::PathToUtf8(relative);
        }
        return UHI::PathToUtf8(path);
    }

    void PublishRegistry(const std::shared_ptr<const UHI::Registry>& registry, const bool restored)
    {
        const auto conflictCount = registry ? registry->Conflicts().size() : 0U;
        {
            std::scoped_lock lock(g_registryMutex);
            g_lastRegistry = registry;
            g_lastConflictCount = conflictCount;
        }
        UHI::SetMenuFrameworkRegistry(registry, restored);
    }

    std::vector<UHI::HotkeyRecord> KnownGameControls()
    {
        const auto gameRoot = std::filesystem::current_path();
        const auto looseControlMap = gameRoot / "Data" / "Interface" / "Controls" / "PC" /
            "controlmap.txt";
        std::error_code error;
        if (std::filesystem::is_regular_file(looseControlMap, error) && !error) {
            auto loose = UHI::Scanners::ControlMapScanner{}.Scan(looseControlMap);
            if (!loose.empty()) return loose;
        }
        return UHI::Scanners::ControlMapScanner{}.KnownGameDefaults(gameRoot);
    }

    std::optional<UHI::ActivationContext> RuntimeControlContext(
        const RE::UserEvents::INPUT_CONTEXT_ID context) noexcept
    {
        using Context = RE::UserEvents::INPUT_CONTEXT_ID;
        switch (context) {
        case Context::kGameplay: return UHI::ActivationContext::gameplay;
        case Context::kMenuMode: return UHI::ActivationContext::menuMode;
        case Context::kConsole: return std::nullopt;
        case Context::kItemMenu: return UHI::ActivationContext::itemMenu;
        case Context::kInventory: return UHI::ActivationContext::inventory;
        case Context::kDebugText: return std::nullopt;
        case Context::kFavorites: return UHI::ActivationContext::favorites;
        case Context::kMap: return UHI::ActivationContext::map;
        case Context::kStats: return UHI::ActivationContext::stats;
        case Context::kCursor: return UHI::ActivationContext::cursor;
        case Context::kBook: return UHI::ActivationContext::book;
        case Context::kDebugOverlay: return std::nullopt;
        case Context::kJournal: return UHI::ActivationContext::journal;
        case Context::kTFCMode: return UHI::ActivationContext::tfcMode;
        case Context::kMapDebug: return std::nullopt;
        case Context::kLockpicking: return UHI::ActivationContext::lockpicking;
        case Context::kFavor: return UHI::ActivationContext::favor;
        default: return std::nullopt;
        }
    }

    bool HiddenRuntimeControlName(std::string value)
    {
        std::ranges::transform(value, value.begin(), [](const unsigned char ch) {
            return static_cast<char>(std::tolower(ch));
        });
        return value.find("console") != std::string::npos ||
            value.find("debug") != std::string::npos ||
            value.find("creation") != std::string::npos ||
            value.find("marketplace") != std::string::npos;
    }

    std::string RuntimeControlIdentity(const UHI::HotkeyRecord& record)
    {
        std::string action = record.action;
        std::ranges::transform(action, action.begin(), [](const unsigned char ch) {
            return static_cast<char>(std::tolower(ch));
        });
        return action + '\x1F' + record.device + '\x1F' + std::to_string(record.contextMask);
    }

    std::vector<UHI::HotkeyRecord> CaptureRuntimeGameControls()
    {
        std::vector<UHI::HotkeyRecord> records;
        const auto* controlMap = RE::ControlMap::GetSingleton();
        if (!controlMap) return records;

        constexpr std::array<std::string_view, 3> devices{ "keyboard", "mouse", "gamepad" };
        const auto looseControlMap = std::filesystem::current_path() / "Data" / "Interface" /
            "Controls" / "PC" / "controlmap.txt";
        for (std::uint32_t contextIndex = 0;
            contextIndex < static_cast<std::uint32_t>(RE::UserEvents::INPUT_CONTEXT_ID::kTotal);
            ++contextIndex) {
            const auto engineContext = static_cast<RE::UserEvents::INPUT_CONTEXT_ID>(contextIndex);
            const auto context = RuntimeControlContext(engineContext);
            if (!context) continue;
            const auto* inputContext = controlMap->controlMap[contextIndex];
            if (!inputContext) continue;
            for (std::size_t deviceIndex = 0; deviceIndex < devices.size(); ++deviceIndex) {
                for (const auto& mapping : inputContext->deviceMappings[deviceIndex]) {
                    const std::string action(mapping.eventID.c_str());
                    if (action.empty() || HiddenRuntimeControlName(action) ||
                        mapping.inputKey == 0xFFU || mapping.inputKey == 0xFFFFU) {
                        continue;
                    }
                    const auto parsed = UHI::ParseControlMapInputCode(
                        std::to_string(mapping.inputKey), devices[deviceIndex]);
                    if (parsed.binding.empty()) continue;
                    std::string binding = parsed.binding;
                    if (mapping.modifier != 0U && mapping.modifier != 0xFFU &&
                        mapping.modifier != 0xFFFFU) {
                        const auto modifier = UHI::ParseControlMapInputCode(
                            std::to_string(mapping.modifier), devices[deviceIndex]);
                        if (!modifier.binding.empty()) binding = modifier.binding + '+' + binding;
                    }
                    records.push_back({
                        .owner = "Skyrim Controls",
                        .action = action,
                        .binding = std::move(binding),
                        .rawBinding = std::to_string(mapping.inputKey),
                        .codeSystem = parsed.codeSystem,
                        .device = std::string(devices[deviceIndex]),
                        .detector = "ControlMapScanner",
                        .confidence = UHI::Confidence::confirmed,
                        .evidencePath = looseControlMap,
                        .stage = UHI::ScanStage::runtime,
                        .editable = false,
                        .runtimeActive = true,
                        .conflictEligible = parsed.conflictEligible,
                        .contextMask = static_cast<std::uint32_t>(*context),
                        .contextConfidence = UHI::ContextConfidence::confirmed
                    });
                }
            }
        }
        return records;
    }

    void OverlayRuntimeGameControls(std::vector<UHI::HotkeyRecord>& records,
        std::vector<UHI::HotkeyRecord> runtimeControls)
    {
        if (runtimeControls.empty()) return;
        std::unordered_map<std::string, UHI::HotkeyRecord> looseByIdentity;
        for (const auto& record : records) {
            if (record.detector == "ControlMapScanner") {
                looseByIdentity.try_emplace(RuntimeControlIdentity(record), record);
            }
        }
        std::unordered_set<std::string> runtimeIdentities;
        runtimeIdentities.reserve(runtimeControls.size());
        for (auto& runtime : runtimeControls) {
            const auto identity = RuntimeControlIdentity(runtime);
            runtimeIdentities.insert(identity);
            if (const auto found = looseByIdentity.find(identity); found != looseByIdentity.end()) {
                runtime.evidencePath = found->second.evidencePath;
                runtime.evidenceLine = found->second.evidenceLine;
                runtime.rawBinding = found->second.rawBinding;
                runtime.editable = found->second.editable;
            }
        }
        std::erase_if(records, [&](const UHI::HotkeyRecord& record) {
            return record.detector == "ControlMapScanner" &&
                runtimeIdentities.contains(RuntimeControlIdentity(record));
        });
        records.insert(records.end(), std::make_move_iterator(runtimeControls.begin()),
            std::make_move_iterator(runtimeControls.end()));
    }

    std::size_t CountAddedOrChangedActiveHotkeys(const UHI::Registry& previous,
        const UHI::Registry& current);

    void RefreshPublishedRuntimeGameControls(const bool notifyChanged)
    {
        auto runtime = CaptureRuntimeGameControls();
        if (runtime.empty()) return;
        std::shared_ptr<const UHI::Registry> current;
        {
            std::scoped_lock lock(g_registryMutex);
            current = g_lastRegistry;
        }
        std::vector<UHI::HotkeyRecord> records;
        if (current) {
            const auto currentRecords = current->Records();
            records.assign(currentRecords.begin(), currentRecords.end());
        }
        OverlayRuntimeGameControls(records, std::move(runtime));
        auto refreshed = std::make_shared<UHI::Registry>();
        for (auto& record : records) refreshed->Add(std::move(record));
        std::shared_ptr<const UHI::Registry> published = std::move(refreshed);
        const auto changedCount = notifyChanged && current ?
            CountAddedOrChangedActiveHotkeys(*current, *published) : 0U;
        PublishRegistry(published, true);
        {
            std::scoped_lock lock(g_registryMutex);
            g_lastComparisonRegistry = published;
        }
        if (changedCount > 0U) UHI::SetMenuFrameworkChangedHotkeyNotice(changedCount);
        SKSE::log::info("Refreshed active Skyrim ControlMap bindings without a filesystem scan");
    }

    void PublishKnownGameControls()
    {
        auto registry = std::make_shared<UHI::Registry>();
        for (auto record : KnownGameControls()) registry->Add(std::move(record));
        const auto count = registry->Records().size();
        std::shared_ptr<const UHI::Registry> published = std::move(registry);
        PublishRegistry(published, false);
        SKSE::log::info("Published {} built-in Skyrim controlmap bindings before the first scan", count);
    }

    bool RestorePreviousScan()
    {
        const auto path = LastScanPath();
        const auto records = UHI::LastScanStore{}.Load(path);
        if (!records) {
            std::error_code error;
            if (std::filesystem::exists(path, error) && !error) {
                SKSE::log::warn("Previous scan snapshot was stale or invalid and was not restored");
                if (const auto comparisonRecords = UHI::LastScanStore{}.Load(path, false)) {
                    auto comparison = std::make_shared<UHI::Registry>();
                    for (auto record : *comparisonRecords) comparison->Add(std::move(record));
                    {
                        std::scoped_lock lock(g_registryMutex);
                        g_lastComparisonRegistry = std::move(comparison);
                    }
                    g_hasValidatedScanSnapshot = true;
                    SKSE::log::info("Retained the stale snapshot only as a hidden change-count baseline");
                }
            }
            return false;
        }
        auto mutableRegistry = std::make_shared<UHI::Registry>();
        for (auto record : *records) mutableRegistry->Add(std::move(record));
        const bool hasControlMap = std::ranges::any_of(mutableRegistry->Records(), [](const auto& record) {
            return record.detector == "ControlMapScanner";
        });
        if (!hasControlMap) {
            for (auto record : KnownGameControls()) mutableRegistry->Add(std::move(record));
        }
        std::shared_ptr<const UHI::Registry> restoredRegistry = std::move(mutableRegistry);
        PublishRegistry(restoredRegistry, true);
        {
            std::scoped_lock lock(g_registryMutex);
            g_lastComparisonRegistry = restoredRegistry;
        }
        g_hasValidatedScanSnapshot = true;
        g_scanPercent = 100.0F;
        {
            std::scoped_lock lock(g_scanStageMutex);
            g_scanStage = "Previous scan restored";
            g_scanPath.clear();
        }
        g_scanFilePercent = 100.0F;
        UHI::SetMenuFrameworkScanStatus(false, 100.0F, 100.0F, "Previous scan restored", "");
        SKSE::log::info("Restored {} hotkeys from the previous validated scan snapshot",
            restoredRegistry->Records().size());
        return true;
    }

    UHI::OpeningHotkey GetOpeningHotkey() noexcept
    {
        auto settings = UnpackOpeningHotkey(g_openingHotkeyPacked.load());
        settings.uiScale = g_uiScale.load();
        settings.windowOpacity = g_windowOpacity.load();
        settings.uiLanguage = static_cast<UHI::UiLanguage>(g_uiLanguage.load());
        return settings;
    }

    bool ApplyOpeningHotkey(const UHI::OpeningHotkey& hotkey)
    {
        if (!UHI::SaveOpeningHotkey(OpeningHotkeyPath(), hotkey)) return false;
        g_openingHotkeyPacked.store(PackOpeningHotkey(hotkey));
        g_uiScale.store(std::clamp(hotkey.uiScale, 0.80F, 1.35F));
        g_windowOpacity.store(std::clamp(hotkey.windowOpacity, 0.35F, 1.0F));
        g_uiLanguage.store(static_cast<std::uint8_t>(hotkey.uiLanguage));
        SKSE::log::info("UHM opening hotkey changed to {}", UHI::FormatOpeningHotkey(hotkey));
        return true;
    }

    bool ToggleOpeningWindow(const std::string_view source)
    {
        // Editor modals and the opening-shortcut capture own all input until
        // they finish. In particular, the currently configured key must not
        // close UHM while the user is trying to replace it.
        if (UHI::IsMenuFrameworkModalInputActive() ||
            UHI::IsMenuFrameworkOpeningHotkeyCaptureActive()) {
            return false;
        }
        const auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
        auto previous = g_lastOpeningToggleMilliseconds.load();
        do {
            if (now - previous < 250) return false;
        } while (!g_lastOpeningToggleMilliseconds.compare_exchange_weak(previous, now));
        const auto hotkey = GetOpeningHotkey();
        const bool toggled = UHI::ToggleMenuFrameworkWindow();
        SKSE::log::info("Opening hotkey {} received via {}; Menu Framework window toggle {}",
            UHI::FormatOpeningHotkey(hotkey), source, toggled ? "succeeded" : "failed");
        return toggled;
    }

#ifdef _WIN32
    void SetUhmGameplayPaused(const bool paused)
    {
        const auto tasks = SKSE::GetTaskInterface();
        if (!tasks) return;
        tasks->AddTask([paused] {
            auto* main = RE::Main::GetSingleton();
            if (!main) return;
            if (paused) {
                if (!main->freezeTime) {
                    main->freezeTime = true;
                    g_uhmPauseOwned = true;
                } else {
                    // A native menu or Menu Framework already owns the pause.
                    g_uhmPauseOwned = false;
                }
                return;
            }
            if (!g_uhmPauseOwned.exchange(false)) return;
            const auto* ui = RE::UI::GetSingleton();
            if (!ui || !ui->IsApplicationMenuOpen()) main->freezeTime = false;
        });
    }

    bool KeyDown(const int virtualKey) noexcept
    {
        return (GetAsyncKeyState(virtualKey) & 0x8000) != 0;
    }

    bool ModifierMatches(const bool required, const std::uint32_t requiredCode,
        const bool leftDown, const bool rightDown) noexcept
    {
        if (!required) return !leftDown && !rightDown;
        if (requiredCode == 0x1D || requiredCode == 0x2A || requiredCode == 0x38) return leftDown;
        if (requiredCode == 0x9D || requiredCode == 0x36 || requiredCode == 0xB8) return rightDown;
        return leftDown || rightDown;
    }

    unsigned ScanCodeToVirtualKey(const std::uint32_t directInputCode) noexcept
    {
        auto scanCode = directInputCode;
        if ((scanCode & 0x80U) != 0) scanCode = 0xE000U | (scanCode & 0x7FU);
        return MapVirtualKeyW(scanCode, MAPVK_VSC_TO_VK_EX);
    }

    bool GameWindowHasFocus() noexcept
    {
        const auto foreground = GetForegroundWindow();
        if (!foreground) return false;
        DWORD process{};
        GetWindowThreadProcessId(foreground, &process);
        return process == GetCurrentProcessId();
    }

    void StartOpeningHotkeyPoll()
    {
        if (g_openingHotkeyPollStarted.exchange(true)) return;
        try {
            std::thread([] {
                SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);
                bool wasDown = false;
                std::uint32_t previousCode{};
                bool previousWindowOpen = false;
                for (;;) {
                    const auto hotkey = GetOpeningHotkey();
                    const auto virtualKey = ScanCodeToVirtualKey(hotkey.scanCode);
                    if (hotkey.scanCode != previousCode) {
                        previousCode = hotkey.scanCode;
                        wasDown = virtualKey != 0 && KeyDown(static_cast<int>(virtualKey));
                    }
                    const bool down = virtualKey != 0 && KeyDown(static_cast<int>(virtualKey));
                    const bool leftCtrl = KeyDown(VK_LCONTROL);
                    const bool rightCtrl = KeyDown(VK_RCONTROL);
                    const bool leftShift = KeyDown(VK_LSHIFT);
                    const bool rightShift = KeyDown(VK_RSHIFT);
                    const bool leftAlt = KeyDown(VK_LMENU);
                    const bool rightAlt = KeyDown(VK_RMENU);
                    const bool uhmOwnsInput = UHI::IsMenuFrameworkModalInputActive() ||
                        UHI::IsMenuFrameworkOpeningHotkeyCaptureActive();
                    if (!uhmOwnsInput && GameWindowHasFocus() && down && !wasDown &&
                        ModifierMatches(hotkey.ctrl, hotkey.ctrlScanCode, leftCtrl, rightCtrl) &&
                        ModifierMatches(hotkey.shift, hotkey.shiftScanCode, leftShift, rightShift) &&
                        ModifierMatches(hotkey.alt, hotkey.altScanCode, leftAlt, rightAlt)) {
                        ToggleOpeningWindow("focused-window key-state fallback");
                    }
                    const bool windowOpen = UHI::IsMenuFrameworkWindowOpen();
                    if (windowOpen != previousWindowOpen) {
                        SetUhmGameplayPaused(windowOpen);
                        previousWindowOpen = windowOpen;
                    }
                    wasDown = down;
                    std::this_thread::sleep_for(std::chrono::milliseconds(12));
                }
            }).detach();
            SKSE::log::info("UHI focused-window opening-key fallback started");
        } catch (const std::exception& error) {
            g_openingHotkeyPollStarted = false;
            SKSE::log::error("Unable to start UHM opening-key fallback: {}", error.what());
        }
    }
#else
    void StartOpeningHotkeyPoll() {}
#endif

    void FinishScan(const bool success, const std::string_view message)
    {
        g_scanPercent = success ? 100.0F : 0.0F;
        g_scanFilePercent = success ? 100.0F : 0.0F;
        g_scanRunning = false;
        {
            std::scoped_lock lock(g_scanStageMutex);
            g_scanStage.assign(message);
            g_scanPath.clear();
        }
        UHI::SetMenuFrameworkScanStatus(false, g_scanPercent.load(), g_scanFilePercent.load(), message, "");
    }

    std::vector<UHI::HotkeyRecord> CaptureSexLabRuntimeHotkeys(const std::filesystem::path& gameRoot)
    {
        std::vector<UHI::HotkeyRecord> records;
        try {
            auto* data = RE::TESDataHandler::GetSingleton();
            auto* vm = RE::BSScript::Internal::VirtualMachine::GetSingleton();
            if (!data || !vm) return records;
            auto* quest = data->LookupForm<RE::TESQuest>(0x00000D62, "SexLab.esm");
            auto* policy = vm->GetObjectHandlePolicy();
            if (!quest || !policy) return records;
            const auto handle = policy->GetHandleForObject(quest->GetFormType(), quest);
            if (handle == policy->EmptyHandle()) return records;

            RE::BSTSmartPointer<RE::BSScript::Object> config;
            if (!vm->FindBoundObject(handle, "sslSystemConfig", config) || !config || !config->IsValid()) {
                return records;
            }
            const auto property = [&](const char* name) -> const RE::BSScript::Variable* {
                return config->GetProperty(RE::BSFixedString(name));
            };
            if (const auto* active = property("SexLabActive"); active && active->IsBool() && !active->GetBool()) {
                return records;
            }
            bool utilityPlus{};
            if (const auto* value = property("HasUtilityPlus"); value && value->IsBool()) {
                utilityPlus = value->GetBool();
            }

            struct Spec
            {
                const char* property;
                const char* action;
                UHI::ActivationContext context;
            };
            static constexpr std::array specs{
                Spec{ "TargetActor", "Target actor for edit", UHI::ActivationContext::sexLabScene },
                Spec{ "ToggleFreeCamera", "Toggle free camera", UHI::ActivationContext::sexLabScene },
                Spec{ "RealignActors", "Realign actors", UHI::ActivationContext::sexLabScene },
                Spec{ "EndAnimation", "End scene (reverse modifier: end all)", UHI::ActivationContext::sexLabScene },
                Spec{ "AdvanceAnimation", "Advance animation stage", UHI::ActivationContext::sexLabScene },
                Spec{ "ChangeAnimation", "Change animation set", UHI::ActivationContext::sexLabScene },
                Spec{ "ChangePositions", "Swap actor positions", UHI::ActivationContext::sexLabScene },
                Spec{ "MoveScene", "Move scene location", UHI::ActivationContext::sexLabScene },
                Spec{ "AdjustStage", "Current/all stages modifier", UHI::ActivationContext::sexLabScene },
                Spec{ "Backwards", "Reverse direction modifier", UHI::ActivationContext::sexLabScene },
                Spec{ "AdjustChange", "Change actor being adjusted", UHI::ActivationContext::sexLabScene },
                Spec{ "AdjustForward", "Move actor forward/backward", UHI::ActivationContext::sexLabScene },
                Spec{ "AdjustUpward", "Move actor up/down", UHI::ActivationContext::sexLabScene },
                Spec{ "AdjustSideways", "Move actor left/right", UHI::ActivationContext::sexLabScene },
                Spec{ "AdjustSchlong", "Adjust schlong", UHI::ActivationContext::sexLabScene },
                Spec{ "RotateScene", "Rotate scene", UHI::ActivationContext::sexLabScene },
                Spec{ "RestoreOffsets", "Delete saved adjustments", UHI::ActivationContext::sexLabScene }
            };
            const auto evidence = gameRoot / "Data" / "Scripts" / "sslSystemConfig.pex";
            for (const auto& spec : specs) {
                const auto* value = property(spec.property);
                if (!value || !value->IsInt()) continue;
                const auto code = value->GetSInt();
                if (code <= 0 || code >= 282) continue;
                const auto parsed = UHI::ParseSkseInputCode(std::to_string(code));
                if (parsed.binding.empty() || !parsed.conflictEligible) continue;
                records.push_back({
                    .owner = utilityPlus ? "SexLab Utility Plus" : "SexLab Framework",
                    .action = spec.action,
                    .binding = parsed.binding,
                    .rawBinding = std::to_string(code),
                    .settingName = spec.property,
                    .codeSystem = parsed.codeSystem,
                    .device = parsed.device,
                    .detector = "PapyrusRuntimeProperty",
                    .categoryHint = "sexlab",
                    .confidence = UHI::Confidence::confirmed,
                    .evidencePath = evidence,
                    .stage = UHI::ScanStage::runtime,
                    .editable = false,
                    .runtimeActive = true,
                    .conflictEligible = true,
                    .contextMask = static_cast<std::uint32_t>(spec.context),
                    .contextConfidence = UHI::ContextConfidence::confirmed
                });
            }
            SKSE::log::info("Captured {} active {} hotkey values from sslSystemConfig",
                records.size(), utilityPlus ? "SexLab Utility Plus" : "SexLab Framework");
        } catch (const std::exception& error) {
            SKSE::log::warn("Unable to read live SexLab hotkey properties safely: {}", error.what());
        } catch (...) {
            SKSE::log::warn("Unable to read live SexLab hotkey properties safely");
        }
        return records;
    }

    struct RuntimeMcmValue
    {
        std::string scriptName;
        std::string modName;
        std::string settingName;
        std::string settingSection;
        std::int32_t keyCode{};
    };

    std::string CanonicalMcmIdentifier(const std::string_view value)
    {
        std::string result;
        result.reserve(value.size());
        for (const auto character : value) {
            if (std::isalnum(static_cast<unsigned char>(character))) {
                result.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(character))));
            }
        }
        if (result.starts_with("temp")) return {};
        if (result.ends_with("var") && result.size() > 3) result.resize(result.size() - 3);
        return result;
    }

    RE::BSScript::Variable* FindScriptVariable(
        const RE::BSTSmartPointer<RE::BSScript::Object>& object, const std::string_view canonicalName)
    {
        if (!object || !object->IsValid()) return nullptr;
        for (auto* type = object->GetTypeInfo(); type; type = type->GetParent()) {
            const auto* variables = type->GetVariableIter();
            for (std::uint32_t index = 0; variables && index < type->GetNumVariables(); ++index) {
                const auto* name = variables[index].name.c_str();
                if (!name || !*name || CanonicalMcmIdentifier(name) != canonicalName) continue;
                if (auto* value = object->GetVariable(variables[index].name)) return value;
            }
            const auto* properties = type->GetPropertyIter();
            for (std::uint32_t index = 0; properties && index < type->GetNumProperties(); ++index) {
                const auto* name = properties[index].name.c_str();
                if (!name || !*name || CanonicalMcmIdentifier(name) != canonicalName) continue;
                if (auto* value = object->GetProperty(properties[index].name)) return value;
            }
        }
        return nullptr;
    }

    std::string McmSettingStem(const std::string_view value)
    {
        auto result = CanonicalMcmIdentifier(value);
        static constexpr std::array prefixes{
            std::string_view("get"), std::string_view("set"), std::string_view("show"),
            std::string_view("check"), std::string_view("toggle"), std::string_view("read"),
            std::string_view("load"), std::string_view("current")
        };
        static constexpr std::array suffixes{
            std::string_view("keybind"), std::string_view("binding"), std::string_view("valueint"),
            std::string_view("value"), std::string_view("bind"), std::string_view("code")
        };
        bool changed = true;
        while (changed && !result.empty()) {
            changed = false;
            for (const auto prefix : prefixes) {
                if (result.size() > prefix.size() && result.starts_with(prefix)) {
                    result.erase(0, prefix.size());
                    changed = true;
                    break;
                }
            }
        }
        for (const auto suffix : suffixes) {
            if (result.size() > suffix.size() && result.ends_with(suffix)) {
                if (suffix == "keybind") result.replace(result.size() - suffix.size(), suffix.size(), "key");
                else result.resize(result.size() - suffix.size());
                break;
            }
        }
        return result;
    }

    void AppendRuntimeMcmValue(std::vector<RuntimeMcmValue>& values, std::unordered_set<std::string>& seen,
        const std::string_view scriptName, const std::string_view modName, const std::string_view settingName,
        const std::string_view settingSection, const std::int32_t keyCode)
    {
        if (scriptName.empty() || settingName.empty()) return;
        const auto identity = CanonicalMcmIdentifier(scriptName) + '\x1F' +
            CanonicalMcmIdentifier(settingSection) + '\x1F' + CanonicalMcmIdentifier(settingName) + '\x1F' +
            std::to_string(keyCode);
        if (!seen.insert(identity).second) return;
        values.push_back({ std::string(scriptName), std::string(modName), std::string(settingName),
            std::string(settingSection), keyCode });
    }

    bool MatchDynamicMcmArrayElement(const std::string_view pattern, const std::string_view candidate,
        std::uint32_t& elementIndex)
    {
        if (!pattern.ends_with("[]") || candidate.size() < 4 || !candidate.ends_with(']')) return false;
        const auto open = candidate.rfind('[');
        if (open == std::string_view::npos || open + 2 > candidate.size()) return false;
        const auto patternBase = pattern.substr(0, pattern.size() - 2);
        const auto candidateBase = candidate.substr(0, open);
        if (CanonicalMcmIdentifier(patternBase) != CanonicalMcmIdentifier(candidateBase)) return false;
        std::uint32_t parsed{};
        for (const auto character : candidate.substr(open + 1, candidate.size() - open - 2)) {
            if (character < '0' || character > '9') return false;
            if (parsed > 4096U) return false;
            parsed = parsed * 10U + static_cast<std::uint32_t>(character - '0');
        }
        elementIndex = parsed;
        return true;
    }

    bool ApplyRuntimeMcmValue(UHI::HotkeyRecord& record, const RuntimeMcmValue& value)
    {
        // Zero is the unmapped sentinel in SkyUI/SKSE's unified key space.
        if (value.keyCode <= 0 || value.keyCode >= 282) return false;
        const auto parsed = UHI::ParseSkseInputCode(std::to_string(value.keyCode));
        if (parsed.binding.empty() || !parsed.conflictEligible) return false;
        record.owner = value.modName.empty() ? record.owner : value.modName;
        record.binding = parsed.binding;
        record.rawBinding = std::to_string(value.keyCode);
        record.codeSystem = parsed.codeSystem;
        record.device = parsed.device;
        record.detector = "PapyrusRuntimeProperty";
        record.settingSection = value.settingSection;
        record.confidence = UHI::Confidence::confirmed;
        record.stage = UHI::ScanStage::runtime;
        // This is an exact integer property on an already-registered SkyUI
        // config instance.  It can be changed safely by the game-thread MCM
        // writer; unresolved/static PEX findings remain read-only.
        record.editable = true;
        record.runtimeActive = true;
        record.conflictEligible = true;
        return true;
    }

    std::string PexScriptName(const std::filesystem::path& path)
    {
        auto text = UHI::PathToUtf8(path);
        if (const auto archive = text.rfind("::"); archive != std::string::npos) text.erase(0, archive + 2U);
        if (const auto slash = text.find_last_of("/\\"); slash != std::string::npos) text.erase(0, slash + 1U);
        if (const auto dot = text.rfind('.'); dot != std::string::npos) text.resize(dot);
        return text;
    }

    std::string McmOptionStem(const std::string_view value)
    {
        auto stem = CanonicalMcmIdentifier(value);
        static constexpr std::array noise{
            std::string_view("option"), std::string_view("keymap"), std::string_view("hotkey"),
            std::string_view("keycode"), std::string_view("value"), std::string_view("list"),
            std::string_view("current"), std::string_view("oid")
        };
        for (const auto token : noise) {
            for (auto at = stem.find(token); at != std::string::npos; at = stem.find(token)) {
                stem.erase(at, token.size());
            }
        }
        if (stem.starts_with("set") && stem.size() > 3U) stem.erase(0, 3U);
        if (stem.ends_with("key") && stem.size() > 3U) stem.resize(stem.size() - 3U);
        return stem;
    }

    struct McmWriteTarget
    {
        RE::BSScript::Variable* variable{};
        RE::TESGlobal* global{};
        RE::BSTSmartPointer<RE::BSScript::Object> configRoot;
        std::int32_t oldValue{};
        int score{};
        std::string name;
    };

    std::optional<std::int32_t> ParseSingleMcmKeyCode(const std::string_view raw)
    {
        std::int32_t value{};
        const auto [end, error] = std::from_chars(raw.data(), raw.data() + raw.size(), value, 10);
        if (error != std::errc{} || end != raw.data() + raw.size() || value < -1 || value >= 282) {
            return std::nullopt;
        }
        return value;
    }

    std::optional<std::int32_t> FindMcmOptionId(
        const RE::BSTSmartPointer<RE::BSScript::Object>& root, const UHI::HotkeyRecord& record)
    {
        if (!root || !root->IsValid()) return std::nullopt;
        const auto settingStem = McmOptionStem(record.settingName);
        const auto actionStem = McmOptionStem(record.action);
        std::optional<std::int32_t> best;
        int bestScore{};
        bool ambiguous{};
        const auto inspect = [&](const RE::BSScript::Variable* variable, const std::string_view name) {
            if (!variable || !variable->IsInt()) return;
            const auto canonical = CanonicalMcmIdentifier(name);
            if (!canonical.contains("oid") && !canonical.starts_with("set") &&
                !canonical.contains("option")) return;
            const auto value = variable->GetSInt();
            if (value <= 0) return;
            const auto candidateStem = McmOptionStem(name);
            int score{};
            if (!settingStem.empty() && candidateStem == settingStem) score = 120;
            else if (!actionStem.empty() && candidateStem == actionStem) score = 115;
            else if (settingStem.size() >= 4U && (candidateStem.contains(settingStem) ||
                         settingStem.contains(candidateStem))) score = 90;
            else if (actionStem.size() >= 4U && (candidateStem.contains(actionStem) ||
                         actionStem.contains(candidateStem))) score = 80;
            if (score > bestScore) {
                best = value;
                bestScore = score;
                ambiguous = false;
            } else if (score != 0 && score == bestScore && best && *best != value) {
                ambiguous = true;
            }
        };
        for (auto* type = root->GetTypeInfo(); type; type = type->GetParent()) {
            const auto* properties = type->GetPropertyIter();
            for (std::uint32_t index = 0; properties && index < type->GetNumProperties(); ++index) {
                const auto* name = properties[index].name.c_str();
                if (name && *name) inspect(root->GetProperty(properties[index].name), name);
            }
            const auto* variables = type->GetVariableIter();
            for (std::uint32_t index = 0; variables && index < type->GetNumVariables(); ++index) {
                const auto* name = variables[index].name.c_str();
                if (name && *name) inspect(root->GetVariable(variables[index].name), name);
            }
        }
        return !ambiguous && bestScore >= 80 ? best : std::nullopt;
    }

    bool ChangeRegisteredMcmHotkey(const UHI::HotkeyRecord& record, const std::int32_t keyCode,
        std::string& detail)
    {
        try {
            auto* vm = RE::BSScript::Internal::VirtualMachine::GetSingleton();
            auto* policy = vm ? vm->GetObjectHandlePolicy() : nullptr;
            auto* managerQuest = RE::TESForm::LookupByEditorID<RE::TESQuest>("SKI_ConfigManagerInstance");
            if (!managerQuest) {
                if (auto* data = RE::TESDataHandler::GetSingleton()) {
                    managerQuest = data->LookupForm<RE::TESQuest>(0x00000802, "SkyUI_SE.esp");
                }
            }
            if (!vm || !policy || !managerQuest) {
                detail = "SkyUI's registered MCM manager is not available.";
                return false;
            }
            const auto handle = policy->GetHandleForObject(managerQuest->GetFormType(), managerQuest);
            RE::BSTSmartPointer<RE::BSScript::Object> manager;
            if (handle == policy->EmptyHandle() ||
                !vm->FindBoundObject(handle, "SKI_ConfigManager", manager) || !manager || !manager->IsValid()) {
                detail = "SkyUI's registered MCM list is not available.";
                return false;
            }
            RE::BSScript::Variable* configsVariable{};
            for (const auto* candidate : { "_modConfigs", "::_modConfigs", "::_modConfigs_var" }) {
                configsVariable = manager->GetVariable(RE::BSFixedString(candidate));
                if (configsVariable && configsVariable->IsArray()) break;
            }
            if (!configsVariable || !configsVariable->IsArray()) {
                configsVariable = FindScriptVariable(manager, "modconfigs");
            }
            const auto configs = configsVariable ? configsVariable->GetArray() : nullptr;
            if (!configs) {
                detail = "SkyUI has no active registered MCM instances.";
                return false;
            }

            const auto targetScript = CanonicalMcmIdentifier(PexScriptName(record.evidencePath));
            const auto targetOwner = CanonicalMcmIdentifier(record.owner);
            const auto targetSetting = CanonicalMcmIdentifier(record.settingName);
            const auto targetStem = McmSettingStem(record.settingName);
            const auto targetSection = CanonicalMcmIdentifier(record.settingSection);
            if (targetSetting.empty()) {
                detail = "The MCM setting identity is missing.";
                return false;
            }

            std::vector<McmWriteTarget> targets;
            std::size_t visitedObjects{};
            constexpr std::size_t kMaximumWriteObjects = 2'048;
            const auto count = (std::min<std::uint32_t>)(configs->size(), 128U);
            for (std::uint32_t rootIndex = 0; rootIndex < count && visitedObjects < kMaximumWriteObjects; ++rootIndex) {
                const auto& element = (*configs)[rootIndex];
                if (!element.IsObject()) continue;
                const auto root = element.GetObject();
                if (!root || !root->IsValid() || !root->GetTypeInfo()) continue;
                std::string modName;
                for (const auto* candidate : { "ModName", "::ModName_var", "modName", "::modName_var" }) {
                    const auto* value = root->GetProperty(RE::BSFixedString(candidate));
                    if (!value) value = root->GetVariable(RE::BSFixedString(candidate));
                    if (value && value->IsString()) { modName = value->GetString(); break; }
                }
                const auto ownerMatches = !targetOwner.empty() &&
                    CanonicalMcmIdentifier(modName) == targetOwner;
                const auto* rootTypeName = root->GetTypeInfo()->GetName();
                const auto rootScript = rootTypeName ? CanonicalMcmIdentifier(rootTypeName) : std::string{};

                const auto walk = [&](const auto& self,
                                      const RE::BSTSmartPointer<RE::BSScript::Object>& object,
                                      const std::string_view section, const std::uint32_t depth) -> void {
                    if (!object || !object->IsValid() || depth > 4U ||
                        visitedObjects++ >= kMaximumWriteObjects) return;
                    for (auto* type = object->GetTypeInfo(); type; type = type->GetParent()) {
                        const auto* definingName = type->GetName();
                        const auto definingScript = definingName ? CanonicalMcmIdentifier(definingName) : std::string{};
                        const bool scriptMatches = !targetScript.empty() &&
                            (definingScript == targetScript || rootScript == targetScript);
                        const auto inspect = [&](RE::BSScript::Variable* variable, const std::string_view name) {
                            if (!variable) return;
                            const auto candidate = CanonicalMcmIdentifier(name);
                            const auto candidateStem = McmSettingStem(name);
                            int settingScore{};
                            if (candidate == targetSetting) settingScore = 120;
                            else if (!targetStem.empty() && candidateStem == targetStem) settingScore = 105;
                            else if (targetStem.size() >= 6U && candidateStem.contains(targetStem)) settingScore = 75;
                            else if (candidateStem.size() >= 6U && targetStem.contains(candidateStem)) settingScore = 70;
                            const auto currentSection = CanonicalMcmIdentifier(section);
                            int score = settingScore + (scriptMatches ? 45 : 0) + (ownerMatches ? 35 : 0);
                            if (!targetSection.empty() && !currentSection.empty() && targetSection == currentSection)
                                score += 20;
                            if (settingScore != 0 && variable->IsInt()) {
                                targets.push_back({ variable, nullptr, root, variable->GetSInt(), score,
                                    std::string(name) });
                            } else if (settingScore != 0 && variable->IsObject()) {
                                if (auto* global = variable->Unpack<RE::TESGlobal*>()) {
                                    targets.push_back({ nullptr, global, root,
                                        static_cast<std::int32_t>(std::lround(global->value)), score,
                                        std::string(name) });
                                }
                            } else if (variable->IsArray()) {
                                const auto array = variable->GetArray();
                                if (!array) return;
                                for (std::uint32_t index = 0; index < array->size(); ++index) {
                                    const auto indexed = std::string(name) + '[' + std::to_string(index) + ']';
                                    if (CanonicalMcmIdentifier(indexed) != targetSetting || !(*array)[index].IsInt())
                                        continue;
                                    targets.push_back({ &(*array)[index], nullptr, root,
                                        (*array)[index].GetSInt(), 150 + (scriptMatches ? 45 : 0) +
                                            (ownerMatches ? 35 : 0), indexed });
                                }
                            }
                            if (depth < 4U && variable->IsObject() && !variable->Unpack<RE::TESGlobal*>()) {
                                const auto nested = variable->GetObject();
                                if (nested && nested->IsValid()) self(self, nested, name, depth + 1U);
                            }
                        };
                        const auto* properties = type->GetPropertyIter();
                        for (std::uint32_t index = 0; properties && index < type->GetNumProperties(); ++index) {
                            const auto* name = properties[index].name.c_str();
                            if (name && *name) inspect(object->GetProperty(properties[index].name), name);
                        }
                        const auto* variables = type->GetVariableIter();
                        for (std::uint32_t index = 0; variables && index < type->GetNumVariables(); ++index) {
                            const auto* name = variables[index].name.c_str();
                            if (name && *name) inspect(object->GetVariable(variables[index].name), name);
                        }
                    }
                };
                walk(walk, root, "", 0);
            }
            if (targets.empty()) {
                detail = "The live MCM property could not be found; no value was changed.";
                return false;
            }
            std::ranges::sort(targets, [](const auto& left, const auto& right) {
                return left.score > right.score;
            });
            if (targets.front().score < 120 || (targets.size() > 1U &&
                targets[0].score == targets[1].score &&
                (targets[0].variable != targets[1].variable || targets[0].global != targets[1].global))) {
                detail = "The live MCM property match is ambiguous; no value was changed.";
                return false;
            }
            auto& target = targets.front();
            const auto section = CanonicalMcmIdentifier(record.settingSection);
            bool dispatched{};
            if (section.contains("addkeymapoptionst") || section.contains("keymapoptionst")) {
                RE::BSTSmartPointer<RE::BSScript::IStackCallbackFunctor> result;
                dispatched = vm->DispatchMethodCall(target.configRoot, "OnKeyMapChangeST",
                    RE::MakeFunctionArguments(std::int32_t{ keyCode }, RE::BSFixedString{}, RE::BSFixedString{}), result);
            } else if (const auto optionId = FindMcmOptionId(target.configRoot, record)) {
                RE::BSTSmartPointer<RE::BSScript::IStackCallbackFunctor> result;
                dispatched = vm->DispatchMethodCall(target.configRoot, "OnOptionKeyMapChange",
                    RE::MakeFunctionArguments(std::int32_t{ *optionId }, std::int32_t{ keyCode },
                        RE::BSFixedString{}, RE::BSFixedString{}), result);
            }
            if (!dispatched) {
                // Fallback is restricted to the exact, unambiguous live
                // property.  This updates what the MCM displays on its next
                // page reset and is serialized with the Papyrus instance.
                if (target.variable) target.variable->SetSInt(keyCode);
                else if (target.global) target.global->value = static_cast<float>(keyCode);
                else return false;
                detail = "The active MCM value was updated; the mod will refresh its key registration when its MCM/page reloads.";
            } else {
                detail = "The mod's MCM remap handler accepted the new key.";
            }
            SKSE::log::info("Changed live MCM binding {} / {} from {} to {} ({})",
                record.owner, target.name, target.oldValue, keyCode, dispatched ? "handler" : "property");
            return true;
        } catch (const std::exception& error) {
            detail = std::string("MCM synchronization failed safely: ") + error.what();
        } catch (...) {
            detail = "MCM synchronization failed safely.";
        }
        return false;
    }

    // Reads only already-registered SkyUI MCM objects. It never invokes a
    // Papyrus function and is deliberately bounded, so a broken menu script
    // cannot stall or crash the scan. Integer properties/variables and their
    // arrays are captured; object-valued properties are followed four levels to
    // cover the common MCM -> settings-script layout.
    std::vector<RuntimeMcmValue> CaptureRegisteredMcmValues()
    {
        std::vector<RuntimeMcmValue> values;
        std::unordered_set<std::string> seen;
        try {
            // A number of classic SkyUI MCMs keep their key-map values in
            // TESGlobal properties.  The PEX scanner knows which Global is an
            // AddKeyMapOption, while this runtime pass supplies the current
            // save-specific value.  Collect only key-shaped editor IDs; they
            // are never emitted on their own and must still join an exact PEX
            // setting name below.
            if (auto* data = RE::TESDataHandler::GetSingleton()) {
                for (auto* global : data->GetFormArray<RE::TESGlobal>()) {
                    if (!global) continue;
                    const auto* editorId = global->GetFormEditorID();
                    if (!editorId || !*editorId) continue;
                    const auto canonical = CanonicalMcmIdentifier(editorId);
                    std::string lowered(editorId);
                    std::ranges::transform(lowered, lowered.begin(), [](const unsigned char character) {
                        return static_cast<char>(std::tolower(character));
                    });
                    const bool keyShaped = canonical.contains("hotkey") || canonical.contains("keycode") ||
                        canonical.contains("keybind") || canonical.contains("shortcut") ||
                        lowered.contains("_hk") || lowered.starts_with("hk_");
                    const auto keyCode = static_cast<std::int32_t>(std::lround(global->value));
                    if (!keyShaped || keyCode <= 0 || keyCode >= 282) continue;
                    std::string pluginName;
                    if (const auto* file = global->GetFile(-1); file && file->fileName) {
                        pluginName = file->fileName;
                    }
                    auto owner = pluginName;
                    if (const auto dot = owner.find_last_of('.'); dot != std::string::npos) owner.resize(dot);
                    AppendRuntimeMcmValue(values, seen, owner.empty() ? "TESGlobal" : owner,
                        owner, editorId, "TESGlobal", keyCode);
                }
            }

            auto* vm = RE::BSScript::Internal::VirtualMachine::GetSingleton();
            auto* policy = vm ? vm->GetObjectHandlePolicy() : nullptr;
            auto* managerQuest = RE::TESForm::LookupByEditorID<RE::TESQuest>("SKI_ConfigManagerInstance");
            if (!managerQuest) {
                if (auto* data = RE::TESDataHandler::GetSingleton()) {
                    managerQuest = data->LookupForm<RE::TESQuest>(0x00000802, "SkyUI_SE.esp");
                }
            }
            if (!vm || !policy || !managerQuest) return values;
            const auto handle = policy->GetHandleForObject(managerQuest->GetFormType(), managerQuest);
            if (handle == policy->EmptyHandle()) return values;
            RE::BSTSmartPointer<RE::BSScript::Object> manager;
            if (!vm->FindBoundObject(handle, "SKI_ConfigManager", manager) || !manager || !manager->IsValid()) {
                return values;
            }

            const RE::BSScript::Variable* configsVariable{};
            for (const auto* candidate : { "_modConfigs", "::_modConfigs", "::_modConfigs_var" }) {
                configsVariable = manager->GetVariable(RE::BSFixedString(candidate));
                if (configsVariable && configsVariable->IsArray()) break;
            }
            if (!configsVariable || !configsVariable->IsArray()) {
                configsVariable = FindScriptVariable(manager, "modconfigs");
            }
            if (!configsVariable || !configsVariable->IsArray()) return values;
            const auto configs = configsVariable->GetArray();
            if (!configs) return values;

            std::size_t visitedObjects{};
            // A large setup can register well over one hundred MCM roots and
            // each root may keep its key settings in one or more nested
            // scripts.  Keep the walk bounded, but do not let early nested
            // objects prevent later registered menus from being inspected.
            constexpr std::size_t kMaximumMcmObjects = 2'048;
            constexpr std::size_t kMaximumMcmValues = 16'384;

            const auto captureObject = [&](const auto& self, const RE::BSTSmartPointer<RE::BSScript::Object>& object,
                                           const std::string_view rootScript, const std::string_view modName,
                                           const std::string_view section, const std::uint32_t depth) -> void {
                if (!object || !object->IsValid() || visitedObjects++ >= kMaximumMcmObjects ||
                    values.size() >= kMaximumMcmValues) return;
                for (auto* type = object->GetTypeInfo(); type && values.size() < kMaximumMcmValues;
                     type = type->GetParent()) {
                    const auto definingScript = type->GetName() ? std::string_view(type->GetName()) : rootScript;
                    const auto ownerScript = rootScript.empty() ? definingScript : rootScript;
                    const auto captureVariable = [&](const RE::BSScript::Variable* variable,
                                                     const std::string_view name) {
                        if (!variable) return;
                        if (variable->IsInt()) {
                            AppendRuntimeMcmValue(values, seen, ownerScript, modName, name, section,
                                variable->GetSInt());
                            if (ownerScript != definingScript) {
                                AppendRuntimeMcmValue(values, seen, definingScript, modName, name, section,
                                    variable->GetSInt());
                            }
                        } else if (variable->IsArray()) {
                            const auto array = variable->GetArray();
                            if (!array || array->size() > 4096) return;
                            for (std::uint32_t index = 0; index < array->size(); ++index) {
                                const auto& element = (*array)[index];
                                const auto indexedName = std::string(name) + '[' + std::to_string(index) + ']';
                                if (element.IsInt()) {
                                    AppendRuntimeMcmValue(values, seen, ownerScript, modName, indexedName, section,
                                        element.GetSInt());
                                } else if (element.IsObject()) {
                                    if (auto* global = element.Unpack<RE::TESGlobal*>()) {
                                        AppendRuntimeMcmValue(values, seen, ownerScript, modName, indexedName, section,
                                            static_cast<std::int32_t>(std::lround(global->value)));
                                    }
                                }
                            }
                        } else if (depth < 4 && variable->IsObject()) {
                            if (auto* global = variable->Unpack<RE::TESGlobal*>()) {
                                AppendRuntimeMcmValue(values, seen, ownerScript, modName, name, section,
                                    static_cast<std::int32_t>(std::lround(global->value)));
                            } else {
                                const auto nested = variable->GetObject();
                                if (nested && nested->IsValid()) {
                                    self(self, nested, ownerScript, modName, name, depth + 1);
                                }
                            }
                        }
                    };

                    const auto* properties = type->GetPropertyIter();
                    for (std::uint32_t index = 0; properties && index < type->GetNumProperties(); ++index) {
                        const auto* name = properties[index].name.c_str();
                        if (!name || !*name) continue;
                        captureVariable(object->GetProperty(properties[index].name), name);
                    }
                    const auto* variables = type->GetVariableIter();
                    for (std::uint32_t index = 0; variables && index < type->GetNumVariables(); ++index) {
                        const auto* name = variables[index].name.c_str();
                        if (!name || !*name) continue;
                        captureVariable(object->GetVariable(variables[index].name), name);
                    }
                }
            };

            const auto count = (std::min<std::uint32_t>)(configs->size(), 128U);
            for (std::uint32_t index = 0; index < count && visitedObjects < kMaximumMcmObjects; ++index) {
                const auto& element = (*configs)[index];
                if (!element.IsObject()) continue;
                const auto config = element.GetObject();
                if (!config || !config->IsValid() || !config->GetTypeInfo()) continue;
                std::string modName;
                if (const auto* value = config->GetProperty(RE::BSFixedString("ModName"));
                    value && value->IsString()) modName = value->GetString();
                const auto* typeName = config->GetTypeInfo()->GetName();
                if (!typeName || !*typeName) continue;
                captureObject(captureObject, config, typeName, modName, "", 0);
            }
            SKSE::log::info("Captured {} live integer values from {} registered MCM/script objects",
                values.size(), visitedObjects);
        } catch (const std::exception& error) {
            SKSE::log::warn("Unable to read registered MCM values safely: {}", error.what());
        } catch (...) {
            SKSE::log::warn("Unable to read registered MCM values safely");
        }
        return values;
    }

    void ResolveRegisteredMcmHotkeys(std::vector<UHI::HotkeyRecord>& records,
        const std::vector<RuntimeMcmValue>& liveValues)
    {
        std::size_t resolvedCount{};
        std::vector<UHI::HotkeyRecord> expandedArrayRecords;
        for (auto& record : records) {
            if (record.detector != "PexBytecodeScanner" || record.settingName.empty()) continue;
            const auto script = CanonicalMcmIdentifier(record.owner);
            const auto setting = CanonicalMcmIdentifier(record.settingName);
            const auto settingStem = McmSettingStem(record.settingName);
            const auto section = CanonicalMcmIdentifier(record.settingSection);
            if (script.empty() || setting.empty()) continue;

            // Some MCMs build all key-map options in a loop from an Int[]. A
            // static PEX pass correctly identifies `Hotkeys[]`, but cannot
            // know the runtime loop index. Expand that template against every
            // live array element on the already-registered MCM instance so no
            // configured key disappears merely because it came from a loop.
            if (record.settingName.ends_with("[]")) {
                std::vector<std::pair<const RuntimeMcmValue*, std::uint32_t>> matches;
                for (const auto& value : liveValues) {
                    if (CanonicalMcmIdentifier(value.scriptName) != script) continue;
                    const auto liveSection = CanonicalMcmIdentifier(value.settingSection);
                    if (!section.empty() && !liveSection.empty() && section != liveSection) continue;
                    std::uint32_t elementIndex{};
                    if (MatchDynamicMcmArrayElement(record.settingName, value.settingName, elementIndex)) {
                        matches.emplace_back(&value, elementIndex);
                    }
                }
                std::ranges::sort(matches, {}, [](const auto& entry) { return entry.second; });
                for (const auto& [value, elementIndex] : matches) {
                    auto expanded = record;
                    expanded.settingName = value->settingName;
                    if (matches.size() > 1) {
                        if (expanded.action.empty() ||
                            CanonicalMcmIdentifier(expanded.action) == CanonicalMcmIdentifier(record.owner)) {
                            expanded.action = value->settingName;
                        } else {
                            expanded.action += " " + std::to_string(elementIndex + 1U);
                        }
                    }
                    if (ApplyRuntimeMcmValue(expanded, *value)) {
                        expandedArrayRecords.push_back(std::move(expanded));
                        ++resolvedCount;
                    }
                }
                continue;
            }
            const RuntimeMcmValue* match{};
            int bestScore{};
            bool ambiguous{};
            for (const auto& value : liveValues) {
                const bool scriptMatches = CanonicalMcmIdentifier(value.scriptName) == script;
                const auto liveSection = CanonicalMcmIdentifier(value.settingSection);
                if (!section.empty() && !liveSection.empty() && section != liveSection) continue;
                const auto liveSetting = CanonicalMcmIdentifier(value.settingName);
                const auto liveStem = McmSettingStem(value.settingName);
                int score{};
                if (scriptMatches && liveSetting == setting) score = 100;
                else if (scriptMatches && !settingStem.empty() && liveStem == settingStem) score = 90;
                else if (scriptMatches && settingStem.size() >= 6 && liveStem.contains(settingStem)) score = 70;
                else if (scriptMatches && liveStem.size() >= 6 && settingStem.contains(liveStem)) score = 65;
                // Global-backed MCM values are owned by the plugin rather than
                // the Papyrus config object.  An exact editor-ID/property-name
                // join is strong enough even though their script names differ.
                else if (liveSetting == setting && liveSection == "tesglobal") score = 85;
                if (score > bestScore) {
                    match = &value;
                    bestScore = score;
                    ambiguous = false;
                } else if (score == bestScore && score != 0 && match &&
                    (match->settingName != value.settingName || match->keyCode != value.keyCode)) {
                    ambiguous = true;
                }
            }
            if (!match || ambiguous || bestScore < 65) continue;
            if (ApplyRuntimeMcmValue(record, *match)) ++resolvedCount;
        }
        records.insert(records.end(), std::make_move_iterator(expandedArrayRecords.begin()),
            std::make_move_iterator(expandedArrayRecords.end()));
        SKSE::log::info("Resolved {} active MCM hotkeys by joining PEX option data to live properties",
            resolvedCount);
    }

    std::string McmOwnerStem(const std::string_view value)
    {
        auto result = CanonicalMcmIdentifier(value);
        static constexpr std::array suffixes{
            std::string_view("configmenu"), std::string_view("settingsmenu"), std::string_view("mcm"),
            std::string_view("config"), std::string_view("settings"), std::string_view("script")
        };
        bool changed = true;
        while (changed && !result.empty()) {
            changed = false;
            for (const auto suffix : suffixes) {
                if (result.size() > suffix.size() && result.ends_with(suffix)) {
                    result.resize(result.size() - suffix.size());
                    changed = true;
                    break;
                }
            }
        }
        return result;
    }

    std::optional<UHI::HotkeyRecord> FindLinkedMcmDocument(const UHI::HotkeyRecord& liveRecord)
    {
        std::shared_ptr<const UHI::Registry> registry;
        {
            std::scoped_lock lock(g_registryMutex);
            registry = g_lastRegistry;
        }
        if (!registry || liveRecord.settingName.empty()) return std::nullopt;
        const auto targetSetting = CanonicalMcmIdentifier(liveRecord.settingName);
        const auto targetStem = McmSettingStem(liveRecord.settingName);
        const auto targetOwner = McmOwnerStem(liveRecord.owner);
        if (targetSetting.empty() || targetOwner.size() < 3U) return std::nullopt;

        const UHI::HotkeyRecord* best{};
        int bestScore{};
        bool ambiguous{};
        for (const auto& candidate : registry->Records()) {
            if (!candidate.editable || !candidate.runtimeActive || candidate.evidencePath.empty() ||
                candidate.detector == "PapyrusRuntimeProperty" ||
                candidate.detector == "ControlMapScanner" ||
                candidate.detector == "BuiltInHotkeyTable") continue;
            const auto candidateSetting = CanonicalMcmIdentifier(candidate.settingName);
            const auto candidateStem = McmSettingStem(candidate.settingName);
            int settingScore{};
            if (candidateSetting == targetSetting) settingScore = 120;
            else if (!targetStem.empty() && candidateStem == targetStem) settingScore = 105;
            if (settingScore == 0) continue;

            const auto candidateOwner = McmOwnerStem(candidate.owner);
            const auto path = CanonicalMcmIdentifier(UHI::PathToUtf8(candidate.evidencePath));
            int ownerScore{};
            if (!candidateOwner.empty() && candidateOwner == targetOwner) ownerScore = 90;
            else if (candidateOwner.size() >= 4U &&
                (candidateOwner.contains(targetOwner) || targetOwner.contains(candidateOwner))) ownerScore = 65;
            else if (path.contains(targetOwner)) ownerScore = 55;
            if (ownerScore == 0) continue;

            int score = settingScore + ownerScore;
            if (candidate.device == liveRecord.device) score += 15;
            if (UHI::NormalizeBinding(candidate.binding) == UHI::NormalizeBinding(liveRecord.binding)) score += 20;
            if (score > bestScore) {
                best = &candidate;
                bestScore = score;
                ambiguous = false;
            } else if (score == bestScore && best &&
                (best->evidencePath != candidate.evidencePath ||
                    best->evidenceLine != candidate.evidenceLine)) {
                ambiguous = true;
            }
        }
        return best && !ambiguous && bestScore >= 175 ? std::optional<UHI::HotkeyRecord>(*best) : std::nullopt;
    }

    std::optional<UHI::SerializedBinding> SerializeMcmKeyForLinkedDocument(
        const UHI::HotkeyRecord& document, const std::int32_t keyCode)
    {
        if (keyCode < 0 || keyCode >= 282) return std::nullopt;
        std::string_view device;
        std::uint32_t capturedCode{};
        if (keyCode < 256) {
            device = "keyboard";
            capturedCode = static_cast<std::uint32_t>(keyCode);
        } else if (keyCode < 266) {
            device = "mouse";
            capturedCode = static_cast<std::uint32_t>(keyCode - 256);
        } else {
            device = "gamepad";
            capturedCode = static_cast<std::uint32_t>(keyCode);
        }
        auto serialized = UHI::SerializeCapturedBinding(document, device, capturedCode);
        if (!serialized.error.empty()) return std::nullopt;
        return serialized;
    }

    void EnrichPersistedMcmHotkeys(std::vector<UHI::HotkeyRecord>& records)
    {
        std::size_t enriched{};
        for (auto& persisted : records) {
            if (!persisted.runtimeActive || persisted.settingName.empty() ||
                persisted.detector == "PexBytecodeScanner" || persisted.detector == "PexScanner") continue;
            const auto setting = CanonicalMcmIdentifier(persisted.settingName);
            if (setting.empty()) continue;
            const auto persistedOwner = McmOwnerStem(persisted.owner);
            const UHI::HotkeyRecord* best{};
            int bestScore{};
            bool ambiguous{};
            for (const auto& pex : records) {
                if (pex.detector != "PexBytecodeScanner" || pex.settingName.empty() ||
                    CanonicalMcmIdentifier(pex.settingName) != setting || pex.action.empty() ||
                    pex.action.starts_with("AddKeyMapOption") || pex.action.starts_with("SetKeyMapOptionValue") ||
                    pex.action.starts_with("RegisterForKey")) continue;
                const auto pexOwner = McmOwnerStem(pex.owner);
                int score = 10;
                if (!persistedOwner.empty() && persistedOwner == pexOwner) score = 100;
                else if (persistedOwner.size() >= 3 && pexOwner.contains(persistedOwner)) score = 80;
                else if (pexOwner.size() >= 3 && persistedOwner.contains(pexOwner)) score = 75;
                const auto path = CanonicalMcmIdentifier(UHI::PathToUtf8(persisted.evidencePath));
                if (!pexOwner.empty() && path.contains(pexOwner)) score += 20;
                if (score > bestScore) {
                    best = &pex;
                    bestScore = score;
                    ambiguous = false;
                } else if (score == bestScore && score != 0 && best && best->owner != pex.owner) {
                    ambiguous = true;
                }
            }
            if (!best || ambiguous || bestScore < 30) continue;
            persisted.action = best->action;
            if (persisted.categoryHint.empty()) persisted.categoryHint = best->categoryHint;
            if (persisted.contextMask == 0) {
                persisted.contextMask = best->contextMask;
                persisted.contextConfidence = best->contextConfidence;
            }
            ++enriched;
        }
        SKSE::log::info("Enriched {} persisted hotkeys with their MCM PEX labels", enriched);
    }

#ifdef _WIN32
    bool IsReadableMemory(const void* address, const std::size_t bytes) noexcept
    {
        if (!address || bytes == 0) return false;
        MEMORY_BASIC_INFORMATION memory{};
        if (VirtualQuery(address, &memory, sizeof(memory)) != sizeof(memory) ||
            memory.State != MEM_COMMIT || (memory.Protect & (PAGE_GUARD | PAGE_NOACCESS)) != 0) return false;
        const auto start = reinterpret_cast<std::uintptr_t>(address);
        const auto base = reinterpret_cast<std::uintptr_t>(memory.BaseAddress);
        return start >= base && bytes <= memory.RegionSize && start - base <= memory.RegionSize - bytes;
    }

    bool IsExecutableAddress(const void* address, HMODULE module) noexcept
    {
        MEMORY_BASIC_INFORMATION memory{};
        if (!address || !module || VirtualQuery(address, &memory, sizeof(memory)) != sizeof(memory) ||
            memory.State != MEM_COMMIT || memory.AllocationBase != module) return false;
        const auto protection = memory.Protect & 0xFFU;
        return protection == PAGE_EXECUTE || protection == PAGE_EXECUTE_READ ||
            protection == PAGE_EXECUTE_READWRITE || protection == PAGE_EXECUTE_WRITECOPY;
    }

    std::vector<UHI::Scanners::ActiveInputSinkTarget> CaptureActiveInputSinks()
    {
        std::vector<const void*> processEvents;
        try {
            const auto* input = RE::BSInputDeviceManager::GetSingleton();
            if (!input) return {};
            {
                // Hold the engine source lock only while copying vtable entries.
                // No sink is invoked and no filesystem/PE work happens here.
                RE::BSSpinLockGuard guard(input->lock);
                processEvents.reserve(input->sinks.size());
                for (const auto* sink : input->sinks) {
                    if (!IsReadableMemory(sink, sizeof(void*))) continue;
                    const auto vtable = *reinterpret_cast<void* const* const*>(sink);
                    if (!IsReadableMemory(vtable, sizeof(void*) * 2U)) continue;
                    // BSTEventSink<InputEvent*>::ProcessEvent is virtual slot 1.
                    const auto processEvent = vtable[1];
                    if (processEvent) processEvents.push_back(processEvent);
                }
            }
        } catch (...) {
            SKSE::log::warn("Unable to copy the active InputEvent sink list safely");
            return {};
        }

        std::vector<UHI::Scanners::ActiveInputSinkTarget> targets;
        std::unordered_set<std::string> seen;
        for (const auto processEvent : processEvents) {
            HMODULE module{};
            if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,
                    reinterpret_cast<LPCWSTR>(processEvent), &module) || !module) continue;
            const auto releaseModule = std::unique_ptr<std::remove_pointer_t<HMODULE>, decltype(&FreeLibrary)>(
                module, &FreeLibrary);
            if (!IsExecutableAddress(processEvent, module)) continue;
            std::wstring buffer(32768U, L'\0');
            const auto length = GetModuleFileNameW(module, buffer.data(), static_cast<DWORD>(buffer.size()));
            if (length == 0 || length >= buffer.size()) continue;
            buffer.resize(length);
            std::filesystem::path modulePath(buffer);
            auto extension = modulePath.extension().wstring();
            std::ranges::transform(extension, extension.begin(), ::towlower);
            if (extension != L".dll") continue;
            auto filename = modulePath.filename().wstring();
            std::ranges::transform(filename, filename.begin(), ::towlower);
            if (filename == L"universalhotkeymanager.dll") continue;
            const auto base = reinterpret_cast<std::uintptr_t>(module);
            const auto address = reinterpret_cast<std::uintptr_t>(processEvent);
            if (address < base || address - base > UINT32_MAX) continue;
            const auto rva = static_cast<std::uint32_t>(address - base);
            auto identity = UHI::PathToUtf8(modulePath.lexically_normal()) + ':' + std::to_string(rva);
            std::ranges::transform(identity, identity.begin(), [](const unsigned char ch) {
                return static_cast<char>(std::tolower(ch));
            });
            if (seen.insert(identity).second) targets.push_back({ std::move(modulePath), rva });
        }
        SKSE::log::info("Captured {} unique active mod InputEvent ProcessEvent handlers from {} sinks",
            targets.size(), processEvents.size());
        return targets;
    }
#else
    std::vector<UHI::Scanners::ActiveInputSinkTarget> CaptureActiveInputSinks()
    {
        return {};
    }
#endif

    std::size_t CountAddedOrChangedActiveHotkeys(const UHI::Registry& previous,
        const UHI::Registry& current)
    {
        const auto identity = [](const UHI::HotkeyRecord& record) {
            // Owner/action are inferred display text and may improve between
            // scans without the actual hotkey changing.  Use the persistent
            // source locator instead so cache restoration and display-name
            // enrichment can never report every restored record as new.
            auto value = UHI::PathToUtf8(record.evidencePath.lexically_normal()) + '\x1F' +
                record.detector + '\x1F' + record.settingSection + '\x1F' +
                record.settingName + '\x1F' + record.device + '\x1F' +
                std::to_string(record.contextMask);
            if (record.settingSection.empty() && record.settingName.empty()) {
                value += '\x1F' + std::to_string(record.evidenceLine);
            }
            std::ranges::transform(value, value.begin(), [](const unsigned char ch) {
                return static_cast<char>(std::tolower(ch));
            });
            return value;
        };
        std::unordered_map<std::string, std::vector<std::string>> oldBindings;
        for (const auto& record : previous.Records()) {
            if (record.runtimeActive && record.conflictEligible)
                oldBindings[identity(record)].push_back(UHI::NormalizeBinding(record.binding));
        }
        std::size_t changed{};
        for (const auto& record : current.Records()) {
            if (!record.runtimeActive || !record.conflictEligible) continue;
            const auto found = oldBindings.find(identity(record));
            if (found == oldBindings.end() || found->second.empty()) {
                ++changed;
                continue;
            }
            const auto binding = UHI::NormalizeBinding(record.binding);
            const auto same = std::ranges::find(found->second, binding);
            if (same != found->second.end()) {
                found->second.erase(same);
            } else {
                // The same persisted setting exists, but its effective key is
                // different. Consume one previous occurrence and count exactly
                // one changed hotkey.
                found->second.pop_back();
                ++changed;
            }
        }
        return changed;
    }

    void LaunchScanWorker(std::string currentSaveName, std::vector<UHI::HotkeyRecord> runtimeHotkeys,
        std::vector<UHI::HotkeyRecord> runtimeGameControls,
        std::vector<RuntimeMcmValue> runtimeMcmValues,
        std::vector<UHI::Scanners::ActiveInputSinkTarget> activeInputSinks,
        const bool automatic)
    {
        try {
            std::thread([currentSaveName = std::move(currentSaveName),
                            runtimeHotkeys = std::move(runtimeHotkeys),
                            runtimeGameControls = std::move(runtimeGameControls),
                            runtimeMcmValues = std::move(runtimeMcmValues),
                            activeInputSinks = std::move(activeInputSinks), automatic]() mutable {
                try {
                    const auto gameRoot = std::filesystem::current_path();
                    // Automatic scans must always compare with the last saved
                    // scan snapshot, not a temporary UI registry published
                    // during save loading.  Loading without fingerprint
                    // validation is intentional: changed fingerprints are the
                    // reason this incremental scan was started, while the old
                    // records remain the authoritative comparison baseline.
                    std::shared_ptr<const UHI::Registry> automaticBaseline;
                    if (automatic) {
                        if (const auto previousRecords = UHI::LastScanStore{}.Load(LastScanPath(), false)) {
                            auto baseline = std::make_shared<UHI::Registry>();
                            for (auto record : *previousRecords) baseline->Add(std::move(record));
                            automaticBaseline = std::move(baseline);
                        }
                    }
                    auto lastUiUpdate = std::chrono::steady_clock::now() - std::chrono::seconds(1);
                    float lastPublishedPercent = -1.0F;
                    float lastPublishedFilePercent = -1.0F;
                    std::string lastPublishedStage;
                    std::string lastPublishedPath;
                    auto records = UHI::ScanPipeline{}.Run(gameRoot, [&](const UHI::ScanProgress& progress) {
                        const auto now = std::chrono::steady_clock::now();
                        const auto currentPath = ScanDisplayPath(progress.currentPath, gameRoot);
                        if (g_scanCancelRequested.load()) {
                            g_scanPercent = progress.overallPercent;
                            g_scanFilePercent = progress.currentFilePercent;
                            UHI::SetMenuFrameworkScanStatus(true, progress.overallPercent,
                                progress.currentFilePercent,
                                "Cancelling...", currentPath);
                            return;
                        }
                        const bool stageChanged = progress.stageName != lastPublishedStage;
                        const bool pathChanged = currentPath != lastPublishedPath;
                        const bool percentAdvanced = progress.overallPercent - lastPublishedPercent >= 0.5F;
                        const bool filePercentAdvanced =
                            progress.currentFilePercent - lastPublishedFilePercent >= 2.0F;
                        const bool intervalElapsed = now - lastUiUpdate >= std::chrono::milliseconds(75);
                        if (!progress.finished && !stageChanged && !pathChanged && !percentAdvanced &&
                            !filePercentAdvanced && !intervalElapsed) return;
                        g_scanPercent = progress.overallPercent;
                        g_scanFilePercent = progress.currentFilePercent;
                        {
                            std::scoped_lock lock(g_scanStageMutex);
                            g_scanStage = progress.stageName;
                            g_scanPath = currentPath;
                        }
                        UHI::SetMenuFrameworkScanStatus(true, progress.overallPercent,
                            progress.currentFilePercent, progress.stageName, currentPath);
                        if (stageChanged || progress.finished) {
                            SKSE::log::info("Scan stage [{}] {}%", progress.stageName, progress.overallPercent);
                        }
                        lastUiUpdate = now;
                        lastPublishedPercent = progress.overallPercent;
                        lastPublishedFilePercent = progress.currentFilePercent;
                        lastPublishedStage = progress.stageName;
                        lastPublishedPath = currentPath;
                    }, [] { return g_scanCancelRequested.load(); }, currentSaveName);

                    if (!g_scanCancelRequested.load() && !activeInputSinks.empty()) {
                        UHI::SetMenuFrameworkScanStatus(true, 100.0F, 100.0F,
                            "Active input handlers", "Registered CommonLib InputEvent sinks");
                        auto activeRecords = UHI::Scanners::PeInputAnalyzer{}.ScanActiveInputSinks(
                            activeInputSinks, [] { return g_scanCancelRequested.load(); });
                        for (auto& record : activeRecords) {
                            UHI::NormalizeRecordDisplayNames(record);
                            record.uiLocalOnly = UHI::IsUiLocalInput(record.owner, record.action,
                                record.detector, record.contextMask);
                        }
                        SKSE::log::info("Resolved {} physical keys from active CommonLib InputEvent handlers",
                            activeRecords.size());
                        records.insert(records.end(), std::make_move_iterator(activeRecords.begin()),
                            std::make_move_iterator(activeRecords.end()));
                    }
                    EnrichPersistedMcmHotkeys(records);
                    ResolveRegisteredMcmHotkeys(records, runtimeMcmValues);
                    OverlayRuntimeGameControls(records, std::move(runtimeGameControls));
                    records.insert(records.end(), std::make_move_iterator(runtimeHotkeys.begin()),
                        std::make_move_iterator(runtimeHotkeys.end()));

                    if (g_scanCancelRequested.load()) {
                        SKSE::log::info("UHI scan cancelled safely by the user");
                        FinishScan(false, "Cancelled");
                        return;
                    }

                    auto mutableRegistry = std::make_shared<UHI::Registry>();
                    for (auto record : records) {
                        mutableRegistry->Add(std::move(record));
                    }
                    std::shared_ptr<const UHI::Registry> completedRegistry = std::move(mutableRegistry);
                    const auto conflictCount = completedRegistry->Conflicts().size();
                    const auto changedCount = automatic && automaticBaseline ?
                        CountAddedOrChangedActiveHotkeys(*automaticBaseline, *completedRegistry) : 0U;
                    PublishRegistry(completedRegistry, false);
                    {
                        std::scoped_lock lock(g_registryMutex);
                        g_lastComparisonRegistry = completedRegistry;
                    }
                    if (changedCount > 0U) UHI::SetMenuFrameworkChangedHotkeyNotice(changedCount);
                    g_hasValidatedScanSnapshot = true;
                    SKSE::log::info("Published {} unique hotkeys ({} duplicate findings removed, {} conflicts)",
                        completedRegistry->Records().size(), completedRegistry->DuplicateCount(), conflictCount);
                    if (completedRegistry->WasTruncated()) {
                        SKSE::log::warn("Registry safety limit reached; omitted {} additional findings",
                            completedRegistry->DroppedCount());
                    }
                    if (const auto logDirectory = SKSE::log::log_directory()) {
                        const auto reportPath = *logDirectory / "UniversalHotkeyManager" / "reports" / "hotkeys.json";
                        try {
                            UHI::JsonReporter{}.Write(reportPath, completedRegistry->Records());
                            SKSE::log::info("Wrote scan report to {}", UHI::PathToUtf8(reportPath));
                        } catch (const std::exception& error) {
                            SKSE::log::error("Unable to write scan report: {}", error.what());
                        }
                    }
                    if (UHI::LastScanStore{}.Save(LastScanPath(), completedRegistry->Records())) {
                        SKSE::log::info("Saved validated previous-scan snapshot");
                    } else {
                        SKSE::log::warn("Unable to save previous-scan snapshot");
                    }
                    FinishScan(true, "Complete");
                } catch (const std::exception& error) {
                    SKSE::log::error("UHI scan failed safely: {}", error.what());
                    FinishScan(false, "Scan failed; see UHI log");
                } catch (...) {
                    SKSE::log::error("UHI scan failed safely with an unknown exception");
                    FinishScan(false, "Scan failed; see UHI log");
                }
            }).detach();
        } catch (const std::exception& error) {
            SKSE::log::error("Unable to create UHI scan worker: {}", error.what());
            FinishScan(false, "Unable to start scan worker");
        }
    }

    void StartScan(const bool automatic = false)
    {
        if (g_gameTransitioning.load()) {
            SKSE::log::info("Scan request ignored during game transition");
            return;
        }
        if (g_scanRunning.exchange(true)) return;
        g_scanCancelRequested = false;
        g_scanFilePercent = 0.0F;
        UHI::SetMenuFrameworkScanStatus(true, 0.0F, 0.0F, "Preparing", "");
        std::string currentSaveName;
        {
            std::scoped_lock lock(g_currentSaveMutex);
            currentSaveName = g_currentSaveName;
        }
        const auto tasks = SKSE::GetTaskInterface();
        if (!tasks) {
            FinishScan(false, "Unable to start scan worker");
            return;
        }
        tasks->AddTask([currentSaveName = std::move(currentSaveName), automatic]() mutable {
            if (g_gameTransitioning.load()) {
                FinishScan(false, "Waiting for game load");
                return;
            }
            const auto gameRoot = std::filesystem::current_path();
            auto runtimeHotkeys = CaptureSexLabRuntimeHotkeys(gameRoot);
            if (g_gameTransitioning.load()) {
                FinishScan(false, "Waiting for game load");
                return;
            }
            auto runtimeMcmValues = CaptureRegisteredMcmValues();
            if (g_gameTransitioning.load()) {
                FinishScan(false, "Waiting for game load");
                return;
            }
            auto activeInputSinks = CaptureActiveInputSinks();
            auto runtimeGameControls = CaptureRuntimeGameControls();
            LaunchScanWorker(std::move(currentSaveName), std::move(runtimeHotkeys),
                std::move(runtimeGameControls),
                std::move(runtimeMcmValues), std::move(activeInputSinks), automatic);
        });
    }

    void QueueBindingWrite(UHI::HotkeyRecord record, std::string newRaw,
        UHI::BindingWriteCompletion completion)
    {
        const auto* tasks = SKSE::GetTaskInterface();
        if (!tasks) {
            if (completion) completion(false, "The SKSE game-thread task service is unavailable.");
            return;
        }
        tasks->AddTask([record = std::move(record), newRaw = std::move(newRaw),
                           completion = std::move(completion)]() mutable {
            bool sourceWritten{};
            bool runtimeWritten{};
            std::string detail;
            try {
                if (g_gameTransitioning.load()) {
                    if (completion) completion(false, "The game is loading; no value was changed.");
                    return;
                }
                if (record.detector == "PapyrusRuntimeProperty") {
                    const auto keyCode = ParseSingleMcmKeyCode(newRaw);
                    if (!keyCode) {
                        if (completion) completion(false,
                            "This MCM option stores one SKSE key code and cannot store that input chord.");
                        return;
                    }
                    std::optional<UHI::HotkeyRecord> linkedMcmDocument;
                    // If this live MCM value is also backed by one uniquely
                    // attributable loose settings record, persist that source
                    // in its own encoding before asking the live MCM to remap.
                    // A generic name such as "Hotkey" is never enough to link
                    // records from different mods.
                    if (auto linked = FindLinkedMcmDocument(record)) {
                        linkedMcmDocument = std::move(linked);
                        const auto serialized = SerializeMcmKeyForLinkedDocument(*linkedMcmDocument, *keyCode);
                        if (!serialized) {
                            if (completion) completion(false,
                                "The linked MCM document cannot represent this input safely; nothing was changed.");
                            return;
                        }
                        sourceWritten = UHI::Writers::ConfigFileWriter{}.SetBinding(
                            linkedMcmDocument->evidencePath, linkedMcmDocument->evidenceLine,
                            linkedMcmDocument->settingName, linkedMcmDocument->rawBinding,
                            serialized->raw);
                        if (!sourceWritten) {
                            if (completion) completion(false,
                                "The linked MCM document changed on disk or could not be written; nothing was changed.");
                            return;
                        }
                    }
                    runtimeWritten = ChangeRegisteredMcmHotkey(record, *keyCode, detail);
                    if (!runtimeWritten) {
                        if (sourceWritten) {
                            // Keep a document-backed MCM option atomic.  A
                            // successful disk write followed by a failed live
                            // remap would otherwise leave the MCM and its source
                            // showing different keys for the rest of the session.
                            // ConfigFileWriter verifies the just-written value
                            // again, so this rollback cannot overwrite a third-
                            // party edit that raced with us.
                            const auto serialized = linkedMcmDocument ?
                                SerializeMcmKeyForLinkedDocument(*linkedMcmDocument, *keyCode) : std::nullopt;
                            const bool rolledBack = linkedMcmDocument && serialized &&
                                UHI::Writers::ConfigFileWriter{}.SetBinding(linkedMcmDocument->evidencePath,
                                    linkedMcmDocument->evidenceLine, linkedMcmDocument->settingName,
                                    serialized->raw, linkedMcmDocument->rawBinding);
                            sourceWritten = false;
                            SKSE::log::warn("Active MCM remap failed after document write; rollback {}: {}",
                                rolledBack ? "succeeded" : "could not be verified", detail);
                            if (completion) completion(false, rolledBack ?
                                "UHM_MCM_DOCUMENT_ROLLED_BACK" :
                                "UHM_MCM_DOCUMENT_ROLLBACK_UNVERIFIED");
                            return;
                        } else {
                            if (completion) completion(false, detail);
                            return;
                        }
                    }
                    if (runtimeWritten) {
                        sourceWritten = true;  // The save-backed live property/handler is the source.
                        if (!linkedMcmDocument) {
                            SKSE::log::info("Changed a save-backed Papyrus MCM value without a document source: {}",
                                detail);
                            detail = "UHM_MCM_GAME_SAVE_REQUIRED";
                        }
                    }
                } else if (record.detector == "ControlMapScanner") {
                    sourceWritten = UHI::Writers::ControlMapWriter{}.SetBinding(
                        record.evidencePath, record.evidenceLine, record.device, newRaw);
                } else {
                    sourceWritten = UHI::Writers::ConfigFileWriter{}.SetBinding(record.evidencePath,
                        record.evidenceLine, record.settingName, record.rawBinding, newRaw);
                    // MCM Helper and similar document-backed menus often keep
                    // a live mirror of the same SKSE integer.  Synchronize it
                    // opportunistically when the source encoding proves that
                    // the value uses SKSE's unified input space.  A document
                    // write remains valid even when that MCM is not currently
                    // registered; it will load the new value next time.
                    auto codeSystem = record.codeSystem;
                    std::ranges::transform(codeSystem, codeSystem.begin(), [](const unsigned char ch) {
                        return static_cast<char>(std::tolower(ch));
                    });
                    if (sourceWritten && codeSystem.contains("skse unified")) {
                        if (const auto keyCode = ParseSingleMcmKeyCode(newRaw)) {
                            std::string runtimeDetail;
                            runtimeWritten = ChangeRegisteredMcmHotkey(record, *keyCode, runtimeDetail);
                            if (runtimeWritten) detail = std::move(runtimeDetail);
                            else SKSE::log::info("Document binding saved; live MCM mirror was unavailable: {}",
                                runtimeDetail);
                        }
                    }
                }
                if (!sourceWritten) {
                    if (completion) completion(false,
                        "The source changed or its exact setting could not be written. No value was changed.");
                    return;
                }
                if (completion) completion(true, detail.empty() ?
                    "The hotkey was saved in its original format." : detail);
                // Papyrus remap handlers are queued by the VM. Give them a
                // short update window before capturing the new runtime value;
                // the detached waiter never touches VM objects itself.
                std::thread([] {
                    std::this_thread::sleep_for(std::chrono::milliseconds(350));
                    if (!g_gameTransitioning.load()) StartScan(false);
                }).detach();
            } catch (const std::exception& error) {
                SKSE::log::error("Binding write failed safely: {}", error.what());
                if (completion) completion(false, std::string("The hotkey could not be saved safely: ") + error.what());
            } catch (...) {
                SKSE::log::error("Binding write failed safely with an unknown exception");
                if (completion) completion(false, "The hotkey could not be saved safely.");
            }
        });
    }

    void CancelScan()
    {
        if (!g_scanRunning.load() || g_scanCancelRequested.exchange(true)) return;
        std::string currentPath;
        {
            std::scoped_lock lock(g_scanStageMutex);
            g_scanStage = "Cancelling...";
            currentPath = g_scanPath;
        }
        UHI::SetMenuFrameworkScanStatus(true, g_scanPercent.load(), g_scanFilePercent.load(),
            "Cancelling...", currentPath);
        SKSE::log::info("Scan cancellation requested; stopping at the next safe file boundary");
    }

    void PapyrusStartScan(RE::BSScript::IVirtualMachine*, RE::VMStackID, RE::StaticFunctionTag*)
    {
        StartScan(false);
    }

    float PapyrusGetScanPercent(RE::BSScript::IVirtualMachine*, RE::VMStackID, RE::StaticFunctionTag*)
    {
        return g_scanPercent.load();
    }

    bool PapyrusIsScanRunning(RE::BSScript::IVirtualMachine*, RE::VMStackID, RE::StaticFunctionTag*)
    {
        return g_scanRunning.load();
    }

    void PapyrusCancelScan(RE::BSScript::IVirtualMachine*, RE::VMStackID, RE::StaticFunctionTag*)
    {
        CancelScan();
    }

    RE::BSFixedString PapyrusGetScanStage(RE::BSScript::IVirtualMachine*, RE::VMStackID, RE::StaticFunctionTag*)
    {
        std::scoped_lock lock(g_scanStageMutex);
        return RE::BSFixedString(g_scanStage);
    }

    RE::BSFixedString PapyrusGetScanPath(RE::BSScript::IVirtualMachine*, RE::VMStackID, RE::StaticFunctionTag*)
    {
        std::scoped_lock lock(g_scanStageMutex);
        return RE::BSFixedString(g_scanPath);
    }

    int PapyrusGetRecordCount(RE::BSScript::IVirtualMachine*, RE::VMStackID, RE::StaticFunctionTag*)
    {
        std::scoped_lock lock(g_registryMutex);
        return static_cast<int>(g_lastRegistry ? g_lastRegistry->Records().size() : 0);
    }

    int PapyrusGetConflictCount(RE::BSScript::IVirtualMachine*, RE::VMStackID, RE::StaticFunctionTag*)
    {
        std::scoped_lock lock(g_registryMutex);
        return static_cast<int>(g_lastConflictCount);
    }

    class ModEventSink final : public RE::BSTEventSink<SKSE::ModCallbackEvent>
    {
    public:
        RE::BSEventNotifyControl ProcessEvent(
            const SKSE::ModCallbackEvent* event,
            RE::BSTEventSource<SKSE::ModCallbackEvent>*) override
        {
            if (!event || event->eventName != "UHI_StartScan" || g_scanRunning.load()) {
                return RE::BSEventNotifyControl::kContinue;
            }

            StartScan(false);
            SKSE::log::info("Full scan requested from MCM");
            return RE::BSEventNotifyControl::kContinue;
        }
    };

    ModEventSink g_modEventSink;

    void RegisterInputSink();

    void OnSkseMessage(SKSE::MessagingInterface::Message* message)
    {
        if (!message) return;
        if (message->type == SKSE::MessagingInterface::kPreLoadGame ||
            message->type == SKSE::MessagingInterface::kDeleteGame) {
            g_gameTransitioning = true;
            CancelScan();
            // Close editor state before the VM and input/menu objects begin
            // reverting.  This also prevents a captured key from leaking into
            // the main menu after a load or return-to-menu transition.
            UHI::CancelMenuFrameworkOpeningHotkeyCapture();
            UHI::CancelMenuFrameworkEditorModal();
            UHI::CloseMenuFrameworkWindow();
        }
        if ((message->type == SKSE::MessagingInterface::kPreLoadGame ||
                message->type == SKSE::MessagingInterface::kSaveGame) && message->data && message->dataLen > 0) {
            const auto* text = static_cast<const char*>(message->data);
            constexpr std::size_t kMaximumSaveNameBytes = 32U * 1024U;
            const auto length = (std::min)(static_cast<std::size_t>(message->dataLen), kMaximumSaveNameBytes);
            std::string saveName(text, text + length);
            if (const auto terminator = saveName.find('\0'); terminator != std::string::npos) saveName.resize(terminator);
            std::scoped_lock lock(g_currentSaveMutex);
            g_currentSaveName = std::move(saveName);
        } else if (message->type == SKSE::MessagingInterface::kNewGame) {
            std::scoped_lock lock(g_currentSaveMutex);
            g_currentSaveName.clear();
        }
        const bool runtimeReady = message->type == SKSE::MessagingInterface::kDataLoaded ||
            message->type == SKSE::MessagingInterface::kPostLoadGame ||
            message->type == SKSE::MessagingInterface::kNewGame;
        if (runtimeReady) {
            g_gameTransitioning = false;
            // The engine's live ControlMap is authoritative for changes made
            // in Skyrim's own Controls menu. Overlay it onto the already
            // published cache immediately; this is an in-memory walk, not a
            // file scan, and therefore does not delay the first UHM frame.
            RefreshPublishedRuntimeGameControls(
                message->type == SKSE::MessagingInterface::kPostLoadGame &&
                g_hasValidatedScanSnapshot.load());
        }
        if (message->type == SKSE::MessagingInterface::kInputLoaded ||
            message->type == SKSE::MessagingInterface::kDataLoaded ||
            message->type == SKSE::MessagingInterface::kPostLoadGame ||
            message->type == SKSE::MessagingInterface::kNewGame) {
            RegisterInputSink();
            StartOpeningHotkeyPoll();
        }
        if (message->type == SKSE::MessagingInterface::kPostLoadGame &&
            g_hasValidatedScanSnapshot.load() && !g_scanRunning.load()) {
            // A restored snapshot already carries fingerprints for its source
            // files. Revalidate those inexpensive metadata fingerprints after
            // loading a save and run the incremental scanner only when one of
            // them actually changed. Opening UHM then remains instant and does
            // not present a redundant 0-100% scan on every load.
            if (UHI::LastScanStore{}.Load(LastScanPath()).has_value()) {
                SKSE::log::info("Save load retained the validated hotkey snapshot; automatic scan skipped");
            } else {
                SKSE::log::info("Save load found changed hotkey evidence; starting incremental scan");
                StartScan(true);
            }
        }
    }

    class InputSink final : public RE::BSTEventSink<RE::InputEvent*>
    {
    public:
        RE::BSEventNotifyControl ProcessEvent(RE::InputEvent* const* events,
            RE::BSTEventSource<RE::InputEvent*>*) override
        {
            for (auto event = events ? *events : nullptr; event; event = event->next) {
                const auto button = event->AsButtonEvent();
                if (!button) continue;

                const auto updateModifier = [&](const std::uint32_t code, bool& state) {
                    if (button->device != RE::INPUT_DEVICE::kKeyboard || button->idCode != code) {
                        return;
                    }
                    if (button->IsDown() || button->IsHeld()) {
                        state = true;
                    } else if (button->IsUp()) {
                        state = false;
                    }
                };
                updateModifier(0x1D, leftCtrlDown_);
                updateModifier(0x9D, rightCtrlDown_);
                updateModifier(0x2A, leftShiftDown_);
                updateModifier(0x36, rightShiftDown_);
                updateModifier(0x38, leftAltDown_);
                updateModifier(0xB8, rightAltDown_);

                if (button->device == RE::INPUT_DEVICE::kKeyboard &&
                    button->idCode == 0x01 && button->IsUp()) {
                    UHI::ReleaseMenuFrameworkEscapeCloseSuppression();
                }

                // Modal state is authoritative.  Menu Framework can update its
                // public window-open flag one frame later than a popup, so
                // gating capture on both flags dropped the first real input.
                if (UHI::IsMenuFrameworkModalInputActive()) {
                    if (button->device == RE::INPUT_DEVICE::kKeyboard &&
                        button->idCode == 0x01 && button->IsDown()) {
                        if (UHI::IsMenuFrameworkBindingCaptureActive())
                            UHI::CancelMenuFrameworkBindingCapture();
                        else
                            UHI::CancelMenuFrameworkEditorModal();
                        ResetModalModifiers();
                        return RE::BSEventNotifyControl::kStop;
                    }

                    if (UHI::IsMenuFrameworkBindingCaptureActive()) {
                        const auto keyboardModifier = button->device == RE::INPUT_DEVICE::kKeyboard &&
                            (button->idCode == 0x1D || button->idCode == 0x9D ||
                             button->idCode == 0x2A || button->idCode == 0x36 ||
                             button->idCode == 0x38 || button->idCode == 0xB8);
                        if (button->device == RE::INPUT_DEVICE::kKeyboard) {
                            if (!keyboardModifier && button->IsDown()) {
                                const auto modifier = leftCtrlDown_ ? 0x1DU : rightCtrlDown_ ? 0x9DU :
                                    leftShiftDown_ ? 0x2AU : rightShiftDown_ ? 0x36U :
                                    leftAltDown_ ? 0x38U : rightAltDown_ ? 0xB8U : 0U;
                                UHI::CaptureMenuFrameworkBindingInput("keyboard", button->idCode,
                                    modifier == 0U ? "" : "keyboard", modifier);
                            } else if (keyboardModifier && button->IsUp()) {
                                UHI::CaptureMenuFrameworkBindingInput("keyboard", button->idCode, "", 0U);
                            }
                        } else if (button->device == RE::INPUT_DEVICE::kMouse) {
                            const bool candidateModifier = button->idCode == 3U || button->idCode == 4U;
                            if (button->IsDown()) {
                                if (candidateModifier && mouseModifier_ == 0U) {
                                    mouseModifier_ = button->idCode + 1U;
                                } else {
                                    const auto modifier = mouseModifier_ == 0U ? 0U : mouseModifier_ - 1U;
                                    UHI::CaptureMenuFrameworkBindingInput("mouse", button->idCode,
                                        modifier == 0U ? "" : "mouse", modifier);
                                    mouseModifier_ = 0U;
                                }
                            } else if (button->IsUp() && mouseModifier_ == button->idCode + 1U) {
                                UHI::CaptureMenuFrameworkBindingInput("mouse", button->idCode, "", 0U);
                                mouseModifier_ = 0U;
                            }
                        } else if (button->device == RE::INPUT_DEVICE::kGamepad) {
                            const auto normalized = SKSE::InputMap::GamepadMaskToKeycode(button->idCode);
                            const bool candidateModifier = normalized == 271U || normalized == 274U ||
                                normalized == 275U || normalized == 280U || normalized == 281U;
                            if (button->IsDown()) {
                                if (candidateModifier && gamepadModifier_ == 0U) {
                                    gamepadModifier_ = normalized;
                                } else {
                                    UHI::CaptureMenuFrameworkBindingInput("gamepad", normalized,
                                        gamepadModifier_ == 0U ? "" : "gamepad", gamepadModifier_);
                                    gamepadModifier_ = 0U;
                                }
                            } else if (button->IsUp() && gamepadModifier_ == normalized) {
                                UHI::CaptureMenuFrameworkBindingInput("gamepad", normalized, "", 0U);
                                gamepadModifier_ = 0U;
                            }
                        }
                    }
                    return RE::BSEventNotifyControl::kStop;
                }

                if (button->device != RE::INPUT_DEVICE::kKeyboard) continue;

                if (UHI::IsMenuFrameworkWindowOpen() &&
                    UHI::IsMenuFrameworkOpeningHotkeyCaptureActive()) {
                    if (button->idCode == 0x01 && button->IsDown()) {
                        UHI::CancelMenuFrameworkOpeningHotkeyCapture();
                        return RE::BSEventNotifyControl::kStop;
                    }
                    const bool modifier = button->idCode == 0x1D || button->idCode == 0x9D ||
                        button->idCode == 0x2A || button->idCode == 0x36 ||
                        button->idCode == 0x38 || button->idCode == 0xB8;
                    if (!modifier && button->IsDown()) {
                        const auto ctrlCode = leftCtrlDown_ ? 0x1DU : rightCtrlDown_ ? 0x9DU : 0U;
                        const auto shiftCode = leftShiftDown_ ? 0x2AU : rightShiftDown_ ? 0x36U : 0U;
                        const auto altCode = leftAltDown_ ? 0x38U : rightAltDown_ ? 0xB8U : 0U;
                        UHI::CaptureMenuFrameworkOpeningHotkey(button->idCode,
                            ctrlCode, shiftCode, altCode);
                    }
                    return RE::BSEventNotifyControl::kStop;
                }

                // Escape belongs to UHM while its window is open. Close only
                // this window and stop the same press from opening Skyrim's
                // system menu underneath it.
                if (button->idCode == 0x01 && button->IsDown() &&
                    UHI::IsMenuFrameworkWindowOpen()) {
                    if (UHI::IsMenuFrameworkEscapeCloseSuppressed())
                        return RE::BSEventNotifyControl::kStop;
                    UHI::CloseMenuFrameworkWindow();
                    return RE::BSEventNotifyControl::kStop;
                }

                const auto openingHotkey = GetOpeningHotkey();
                if (button->idCode == openingHotkey.scanCode && button->IsDown() &&
                    ModifierMatches(openingHotkey.ctrl, openingHotkey.ctrlScanCode,
                        leftCtrlDown_, rightCtrlDown_) &&
                    ModifierMatches(openingHotkey.shift, openingHotkey.shiftScanCode,
                        leftShiftDown_, rightShiftDown_) &&
                    ModifierMatches(openingHotkey.alt, openingHotkey.altScanCode,
                        leftAltDown_, rightAltDown_)) {
                    ToggleOpeningWindow("Skyrim input event");
                }
            }
            return RE::BSEventNotifyControl::kContinue;
        }

    private:
        void ResetModalModifiers() noexcept
        {
            mouseModifier_ = 0U;
            gamepadModifier_ = 0U;
        }

        bool leftCtrlDown_{ false };
        bool rightCtrlDown_{ false };
        bool leftShiftDown_{ false };
        bool rightShiftDown_{ false };
        bool leftAltDown_{ false };
        bool rightAltDown_{ false };
        std::uint32_t mouseModifier_{};
        std::uint32_t gamepadModifier_{};
    };

    InputSink g_inputSink;
    std::atomic_bool g_inputSinkRegistered{ false };

    void RegisterInputSink()
    {
        if (g_inputSinkRegistered.load()) return;
        try {
            if (const auto input = RE::BSInputDeviceManager::GetSingleton()) {
                bool expected = false;
                if (g_inputSinkRegistered.compare_exchange_strong(expected, true)) {
                    input->AddEventSink<RE::InputEvent*>(&g_inputSink);
                    // Menu Framework and other UI sinks may stop propagation.
                    // UHM must see the event first so its modal capture can
                    // record it and then deliberately block every downstream
                    // shortcut. Outside capture this sink always continues.
                    {
                        RE::BSSpinLockGuard guard(input->lock);
                        const auto found = std::find(input->sinks.begin(), input->sinks.end(), &g_inputSink);
                        if (found != input->sinks.end() && found != input->sinks.begin()) {
                            std::rotate(input->sinks.begin(), found, std::next(found));
                        }
                    }
                    SKSE::log::info("UHI priority input sink registered");
                }
            } else {
                SKSE::log::warn("UHI input manager was not ready; registration will be retried");
            }
        } catch (...) {
            g_inputSinkRegistered = false;
            SKSE::log::error("Unable to register UHI input sink safely");
        }
    }

    void InitializeLogging()
    {
        auto logDirectory = SKSE::log::log_directory();
        if (!logDirectory) {
            SKSE::stl::report_and_fail("Unable to resolve the SKSE log directory");
        }

        const auto logPath = *logDirectory / "UniversalHotkeyManager.log";
        auto sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(logPath.string(), true);
        auto logger = std::make_shared<spdlog::logger>("global", std::move(sink));
        logger->set_level(spdlog::level::info);
        logger->flush_on(spdlog::level::info);
        spdlog::set_default_logger(std::move(logger));
    }

}

bool RegisterPapyrus(RE::BSScript::IVirtualMachine* vm)
{
    vm->RegisterFunction("StartScan", "UHI_MCM", PapyrusStartScan);
    vm->RegisterFunction("GetScanPercent", "UHI_MCM", PapyrusGetScanPercent);
    vm->RegisterFunction("IsScanRunning", "UHI_MCM", PapyrusIsScanRunning);
    vm->RegisterFunction("CancelScan", "UHI_MCM", PapyrusCancelScan);
    vm->RegisterFunction("GetScanStage", "UHI_MCM", PapyrusGetScanStage);
    vm->RegisterFunction("GetScanPath", "UHI_MCM", PapyrusGetScanPath);
    vm->RegisterFunction("GetRecordCount", "UHI_MCM", PapyrusGetRecordCount);
    vm->RegisterFunction("GetConflictCount", "UHI_MCM", PapyrusGetConflictCount);
    return true;
}

SKSEPluginLoad(const SKSE::LoadInterface* skse)
{
    InitializeLogging();
    SKSE::Init(skse);
    const auto openingHotkey = UHI::LoadOpeningHotkey(OpeningHotkeyPath());
    g_openingHotkeyPacked.store(PackOpeningHotkey(openingHotkey));
    g_uiScale.store(openingHotkey.uiScale);
    g_windowOpacity.store(openingHotkey.windowOpacity);
    g_uiLanguage.store(static_cast<std::uint8_t>(openingHotkey.uiLanguage));
    SKSE::GetPapyrusInterface()->Register(RegisterPapyrus);
    const bool menuRegistered = UHI::RegisterMenuFrameworkWindow();
    SKSE::log::info("UHI Menu Framework window registration {}",
        menuRegistered ? "succeeded" : "failed");
    UHI::SetMenuFrameworkStartScan([] { StartScan(false); });
    UHI::SetMenuFrameworkAutomaticRefresh([] {
        if (g_hasValidatedScanSnapshot.load() && !g_scanRunning.load() &&
            !UHI::LastScanStore{}.Load(LastScanPath()).has_value()) {
            StartScan(true);
        }
    });
    UHI::SetMenuFrameworkCancelScan(CancelScan);
    UHI::SetMenuFrameworkOpeningHotkey(GetOpeningHotkey, ApplyOpeningHotkey);
    UHI::SetMenuFrameworkBindingWriter(QueueBindingWrite);
    const bool sexLabInstalled = UHI::IsSexLabInstalled(std::filesystem::current_path());
    UHI::SetMenuFrameworkSexLabInstalled(sexLabInstalled);
    SKSE::log::info("SexLab installation marker {}", sexLabInstalled ? "detected" : "not detected");
    if (!RestorePreviousScan()) PublishKnownGameControls();
    if (const auto messaging = SKSE::GetMessagingInterface()) {
        messaging->RegisterListener(OnSkseMessage);
    }
    if (const auto source = SKSE::GetModCallbackEventSource()) {
        source->AddEventSink(&g_modEventSink);
    }
    // Scanning starts only from the in-game Scan button/MCM event; plugin load
    // performs no filesystem traversal or report generation. The input sink is
    // attached only after SKSE sends kInputLoaded, never against a partially
    // initialized input manager.
    SKSE::log::info("Universal Hotkey Manager for Skyrim SE-AE {} loaded; opening key {}; scan is on-demand",
        UHI_VERSION, UHI::FormatOpeningHotkey(openingHotkey));
    return true;
}
