#include "UHI/scanners/DllScanner.h"
#include "UHI/PathEncoding.h"
#include "UHI/ConfigBindingParser.h"
#include "UHI/scanners/PeInputAnalyzer.h"
#include "UHI/GameFilePolicy.h"
#include "UHI/ScanWorker.h"
#include "UHI/ActivationContextInference.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <fstream>
#include <regex>
#include <string>
#include <unordered_set>
#include <vector>
#include <atomic>
#include <functional>
#include <mutex>
#include <thread>

namespace
{
    constexpr std::size_t kChunkBytes = 1024U * 1024U;
    constexpr std::size_t kOverlapBytes = 1024U;
    constexpr std::uintmax_t kMaximumDllBytes = 256U * 1024U * 1024U;

    bool IsDll(const std::filesystem::path& path)
    {
        auto extension = UHI::PathToUtf8(path.extension());
        std::ranges::transform(extension, extension.begin(), [](const unsigned char character) {
            return static_cast<char>(std::tolower(character));
        });
        return extension == ".dll";
    }

    constexpr std::array<std::string_view, 6> apiNames{
        "GetAsyncKeyState", "GetKeyState", "RegisterRawInputDevices",
        "SetWindowsHookEx", "DirectInput8Create", "RegisterHotKey"
    };

    bool ContainsAsciiInsensitive(const std::string_view bytes, const std::string_view needle)
    {
        if (needle.empty() || bytes.size() < needle.size()) return false;
        const auto equal = [](const char left, const char right) {
            return std::tolower(static_cast<unsigned char>(left)) ==
                std::tolower(static_cast<unsigned char>(right));
        };
        return std::search(bytes.begin(), bytes.end(), needle.begin(), needle.end(), equal) != bytes.end();
    }

    struct GeneratedSettingsEvidence
    {
        bool typeModeSettingsPath{};
        bool typeModeKey{};
        bool typeModeLabel{};
    };

    void ObserveGeneratedSettings(const std::string_view bytes, GeneratedSettingsEvidence& evidence)
    {
        // Some SKSE Menu Framework pages create their JSON only after the
        // player changes/saves a setting.  Until then the effective key lives
        // solely in the loaded plugin's compiled defaults.  Keep these
        // signatures cumulative because the strings need not share one DLL
        // read window.
        evidence.typeModeSettingsPath = evidence.typeModeSettingsPath ||
            ContainsAsciiInsensitive(bytes, "TypeMode_Settings.json");
        evidence.typeModeKey = evidence.typeModeKey ||
            ContainsAsciiInsensitive(bytes, "TypeMode_k");
        evidence.typeModeLabel = evidence.typeModeLabel ||
            ContainsAsciiInsensitive(bytes, "Typing mode Key");
    }

    void AddGeneratedSettingsDefaults(std::vector<UHI::HotkeyRecord>& records,
        const std::filesystem::path& modulePath, const GeneratedSettingsEvidence& evidence)
    {
        if (!evidence.typeModeSettingsPath || !evidence.typeModeKey || !evidence.typeModeLabel ||
            records.size() >= UHI::kMaximumCollectedRecords) return;

        const auto settingsPath = modulePath.parent_path() / "TypeMode_Settings.json";
        std::error_code error;
        if (std::filesystem::exists(settingsPath, error) && !error) {
            // The configuration scanner reads the actual VFS winner.  Do not
            // add the compiled fallback as a second, stale binding.
            return;
        }
        const auto duplicate = std::ranges::find_if(records, [](const auto& record) {
            return record.settingName == "TypeMode_k" ||
                (record.owner == "Typing Mode" && record.binding == "F4");
        });
        if (duplicate != records.end()) return;

        // Type Mode's public source declares `inline uint32_t TypeMode_k = 62`.
        // 62 is the DirectInput scan code for F4.  The three independent
        // strings above identify that exact settings schema in the binary,
        // avoiding a filename-only special case or arbitrary-number search.
        const auto parsed = UHI::ParseConfigBinding(
            "TypeMode_k", "62", UHI::NumericCodeSpace::directInputScanCode);
        records.push_back({
            .owner = "Typing Mode", .action = "Typing Mode", .binding = parsed.binding,
            .rawBinding = "62", .settingName = "TypeMode_k",
            .codeSystem = parsed.codeSystem + " (compiled setting default)",
            .device = parsed.device, .detector = "GeneratedSettingsDefaultScanner",
            .categoryHint = "interface", .confidence = UHI::Confidence::inferred,
            .evidencePath = modulePath, .evidenceLine = 0,
            .stage = UHI::ScanStage::nativePlugins, .editable = false,
            .runtimeActive = true, .conflictEligible = parsed.conflictEligible
        });
    }

    bool MayContainEmbeddedBinding(const std::string_view bytes)
    {
        static constexpr std::array<std::string_view, 9> indicators{
            "hotkey", "keybind", "keycode", "scancode", "virtualkey", "keymap", "inputkey", "ikey=", "ikey:"
        };
        return std::ranges::any_of(indicators, [&](const auto indicator) {
            return ContainsAsciiInsensitive(bytes, indicator);
        });
    }

    UHI::NumericCodeSpace EmbeddedCodeSpace(std::string key)
    {
        std::ranges::transform(key, key.begin(), [](const unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
        if (key.find("virtualkey") != std::string::npos || key.find("vkcode") != std::string::npos)
            return UHI::NumericCodeSpace::windowsVirtualKey;
        if (key.find("scancode") != std::string::npos || key.find("dik") != std::string::npos ||
            key.find("directinput") != std::string::npos || key.find("dxkey") != std::string::npos)
            return UHI::NumericCodeSpace::directInputScanCode;
        if (key.find("skse") != std::string::npos || key.find("keymap") != std::string::npos ||
            key.find("inputmap") != std::string::npos)
            return UHI::NumericCodeSpace::skseUnifiedInputCode;
        return UHI::NumericCodeSpace::unknown;
    }

    std::string EmbeddedActionName(std::string key, const std::string_view owner)
    {
        auto lowered = key;
        std::ranges::transform(lowered, lowered.begin(), [](const unsigned char character) {
            return static_cast<char>(std::tolower(character));
        });
        auto compact = lowered;
        std::erase_if(compact, [](const unsigned char character) { return !std::isalnum(character); });
        static const std::unordered_set<std::string> generic{
            "hotkey", "key", "ikey", "keycode", "scancode", "virtualkey", "inputkey",
            "hotkeycode", "keymap", "keybind", "shortcut", "keycustom", "keydefine"
        };
        if (generic.contains(compact)) return std::string(owner);

        const auto removeSuffix = [&](const std::string_view suffix) {
            if (lowered.size() <= suffix.size() || !lowered.ends_with(suffix)) return false;
            key.resize(key.size() - suffix.size());
            lowered.resize(lowered.size() - suffix.size());
            return true;
        };
        const auto removePrefix = [&](const std::string_view prefix) {
            if (lowered.size() <= prefix.size() || !lowered.starts_with(prefix)) return false;
            key.erase(0, prefix.size());
            lowered.erase(0, prefix.size());
            return true;
        };
        for (const auto suffix : { "hotkey", "shortcut", "keybind", "keycode", "scancode",
                 "virtualkey", "inputkey", "keymap", "key" }) {
            if (removeSuffix(suffix)) break;
        }
        for (const auto prefix : { "hotkey", "shortcut", "keybind", "keycode", "scancode",
                 "virtualkey", "inputkey", "keymap", "key" }) {
            if (removePrefix(prefix)) break;
        }
        if (key.empty()) return std::string(owner);
        std::string result;
        for (std::size_t index = 0; index < key.size(); ++index) {
            const auto character = static_cast<unsigned char>(key[index]);
            if (character == '_' || character == '-' || character == '.') {
                if (!result.empty() && result.back() != ' ') result.push_back(' ');
                continue;
            }
            if (index > 0U && std::isupper(character) &&
                std::islower(static_cast<unsigned char>(key[index - 1])) && !result.empty() && result.back() != ' ') {
                result.push_back(' ');
            }
            result.push_back(static_cast<char>(character));
        }
        return result.empty() ? std::string(owner) : result;
    }

    void AddEmbeddedBinding(std::vector<UHI::HotkeyRecord>& records,
        const std::filesystem::path& path, const std::string_view bytes)
    {
        if (records.size() >= UHI::kMaximumCollectedRecords) return;
        // Native mods often retain default INI/JSON fragments inside the DLL.
        // Only accept a named assignment, not arbitrary nearby numeric bytes.
        static const std::regex assignment(
            R"(([A-Za-z0-9_.-]*(?:hotkey|keybind|keycode|scancode|virtualkey|keymap|inputkey|ikey)[A-Za-z0-9_.-]*)\s*[:=]\s*["']?([A-Za-z0-9+_]{1,32}))",
            std::regex::icase);
        // Never run std::regex over megabytes of arbitrary executable bytes.
        // Extract bounded printable strings first; compiled INI/JSON defaults
        // live in those runs while code and compressed data are discarded.
        for (std::size_t offset = 0; offset < bytes.size();) {
            while (offset < bytes.size() &&
                (static_cast<unsigned char>(bytes[offset]) < 0x20U ||
                    static_cast<unsigned char>(bytes[offset]) > 0x7EU)) ++offset;
            const auto start = offset;
            while (offset < bytes.size() && static_cast<unsigned char>(bytes[offset]) >= 0x20U &&
                static_cast<unsigned char>(bytes[offset]) <= 0x7EU) ++offset;
            if (offset - start < 4U) continue;
            constexpr std::size_t chunkBytes = 16U * 1024U;
            constexpr std::size_t overlapBytes = 256U;
            for (std::size_t position = start; position < offset;) {
                const auto length = (std::min)(chunkBytes, offset - position);
                const std::string token(bytes.substr(position, length));
                if (MayContainEmbeddedBinding(token)) {
                    for (std::sregex_iterator it(token.begin(), token.end(), assignment), end; it != end; ++it) {
                        if (records.size() >= UHI::kMaximumCollectedRecords) break;
                        const auto key = (*it)[1].str();
                        const auto raw = (*it)[2].str();
                        const auto parsed = UHI::ParseConfigBinding(key, raw, EmbeddedCodeSpace(key));
                        if (parsed.binding.empty()) continue;
                        records.push_back({
                            .owner = UHI::PathToUtf8(path.stem()), .action = "Embedded default: " + key,
                            .binding = parsed.binding, .rawBinding = raw, .codeSystem = parsed.codeSystem,
                            .device = parsed.device, .detector = "DllScanner",
                            .confidence = parsed.conflictEligible ? UHI::Confidence::inferred : UHI::Confidence::candidate,
                            .evidencePath = path, .evidenceLine = 0, .stage = UHI::ScanStage::nativePlugins,
                            .conflictEligible = parsed.conflictEligible
                        });
                    }
                }
                if (position + length >= offset) break;
                position += length - overlapBytes;
            }
            if (records.size() >= UHI::kMaximumCollectedRecords) break;
        }
    }

    void ResolveEmbeddedFallbacks(std::vector<UHI::HotkeyRecord>& records,
        const std::array<bool, apiNames.size()>& apiFound)
    {
        // A DLL that imports the Win32 key-state/hotkey APIs most commonly feeds
        // them VK values. Otherwise SKSE/CommonLib plugins most commonly expose
        // DirectInput keyboard codes plus the SKSE mouse/gamepad continuation.
        const bool win32KeyApi = apiFound[0] || apiFound[1] || apiFound[5];
        const bool directInputApi = apiFound[4];
        const auto fallback = win32KeyApi && !directInputApi ?
            UHI::NumericCodeSpace::windowsVirtualKey : UHI::NumericCodeSpace::skseUnifiedInputCode;
        const auto reason = win32KeyApi && !directInputApi ?
            " (DLL Win32-key-API fallback)" : " (DLL SKSE/CommonLib fallback)";

        constexpr std::string_view prefix = "Embedded default: ";
        for (auto& record : records) {
            if (!record.action.starts_with(prefix)) continue;
            const auto key = record.action.substr(prefix.size());
            if (!record.conflictEligible) {
                auto parsed = UHI::ParseConfigBinding(key, record.rawBinding, fallback);
                if (parsed.conflictEligible) {
                    record.binding = std::move(parsed.binding);
                    record.device = std::move(parsed.device);
                    record.codeSystem = std::move(parsed.codeSystem) + reason;
                    record.confidence = UHI::Confidence::inferred;
                    record.conflictEligible = true;
                }
            }
            record.action = EmbeddedActionName(key, record.owner);
        }
    }

    std::vector<UHI::HotkeyRecord> ScanDll(const std::filesystem::path& path,
        const UHI::CancelCallback& cancel, const std::function<void(float)>& fileProgress)
    {
        std::vector<UHI::HotkeyRecord> found;
        std::ifstream input(path, std::ios::binary);
        if (!input) return found;
        std::array<bool, apiNames.size()> apiFound{};
        bool commonLibInputEvidence{};
        GeneratedSettingsEvidence generatedSettings;
        UHI::ContextInference activation;
        std::vector<char> buffer(kChunkBytes);
        std::string overlap;
        std::error_code sizeError;
        const auto fileBytes = std::filesystem::file_size(path, sizeError);
        std::uintmax_t processedBytes{};
        while (input && !UHI::ScanCancelled(cancel) && found.size() < UHI::kMaximumCollectedRecords) {
            input.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
            const auto count = input.gcount();
            if (count <= 0) break;
            std::string window = overlap;
            window.append(buffer.data(), static_cast<std::size_t>(count));
            ObserveGeneratedSettings(window, generatedSettings);
            if (MayContainEmbeddedBinding(window)) AddEmbeddedBinding(found, path, window);
            if (!commonLibInputEvidence &&
                (ContainsAsciiInsensitive(window, "ButtonEvent") ||
                    ContainsAsciiInsensitive(window, "InputEvent"))) {
                commonLibInputEvidence = true;
            }
            const auto chunkActivation = UHI::InferActivationContext(
                window, UHI::ContextEvidenceSource::nativeBinary);
            activation.mask |= chunkActivation.mask;
            if (static_cast<unsigned>(chunkActivation.confidence) >
                static_cast<unsigned>(activation.confidence)) {
                activation.confidence = chunkActivation.confidence;
            }
            for (std::size_t index = 0; index < apiNames.size(); ++index) {
                if (!apiFound[index] && window.find(apiNames[index]) != std::string::npos) apiFound[index] = true;
            }
            const auto retained = std::min(kOverlapBytes, window.size());
            overlap.assign(window.end() - static_cast<std::ptrdiff_t>(retained), window.end());
            processedBytes += static_cast<std::uintmax_t>(count);
            if (fileProgress) {
                const auto fraction = sizeError || fileBytes == 0U ? 1.0F :
                    static_cast<float>(processedBytes) / static_cast<float>(fileBytes);
                fileProgress(5.0F + std::clamp(fraction, 0.0F, 1.0F) * 65.0F);
            }
        }
        if (fileProgress) fileProgress(72.0F);
        ResolveEmbeddedFallbacks(found, apiFound);
        AddGeneratedSettingsDefaults(found, path, generatedSettings);
        if (activation.mask != 0) {
            for (auto& record : found) {
                if (record.contextMask != 0) continue;
                record.contextMask = activation.mask;
                record.contextConfidence = activation.confidence;
            }
        }
        std::unordered_set<std::string> seen;
        std::erase_if(found, [&](const auto& record) {
            return !seen.insert(record.action + '\x1F' + record.binding).second;
        });
        for (std::size_t index = 0; index < apiNames.size(); ++index) {
            if (!apiFound[index] || found.size() >= UHI::kMaximumCollectedRecords) continue;
            found.push_back({
                .owner = UHI::PathToUtf8(path.stem()), .action = "Native input API pattern",
                .binding = std::string(apiNames[index]) + " (key unresolved)",
                .rawBinding = std::string(apiNames[index]), .codeSystem = "native API evidence (value unresolved)",
                .device = "unknown", .detector = "DllScanner",
                .confidence = UHI::Confidence::candidate, .evidencePath = path, .evidenceLine = 0,
                .stage = UHI::ScanStage::nativePlugins, .conflictEligible = false
            });
        }
        const bool resolvableImportedApi = apiFound[0] || apiFound[1] || apiFound[5];
        if (resolvableImportedApi && !UHI::ScanCancelled(cancel) &&
            found.size() < UHI::kMaximumCollectedRecords) {
            if (fileProgress) fileProgress(80.0F);
            auto analyzed = UHI::Scanners::PeInputAnalyzer{}.Scan(path, cancel);
            UHI::AppendScanResults(found, analyzed);
            if (fileProgress) fileProgress(90.0F);
        }
        if (commonLibInputEvidence && !UHI::ScanCancelled(cancel) &&
            found.size() < UHI::kMaximumCollectedRecords) {
            if (fileProgress) fileProgress(92.0F);
            auto analyzed = UHI::Scanners::PeInputAnalyzer{}.ScanStaticInputHandlers(path, cancel);
            UHI::AppendScanResults(found, analyzed);
            if (fileProgress) fileProgress(98.0F);
        }
        if (fileProgress) fileProgress(99.0F);
        return found;
    }
}

namespace UHI::Scanners
{
    std::vector<HotkeyRecord> DllScanner::Scan(const std::filesystem::path& root,
        const CancelCallback& cancel, const PathCallback& path, ScanCache* cache,
        const std::size_t workers, const ItemProgressCallback& itemProgress) const
    {
        std::vector<HotkeyRecord> records;
        std::error_code error;
        if (!std::filesystem::exists(root, error)) return records;
        std::vector<std::filesystem::path> candidates;
        std::filesystem::recursive_directory_iterator iterator(root,
            std::filesystem::directory_options::skip_permission_denied, error), end;
        while (iterator != end) {
            if (ScanCancelled(cancel)) break;
            if (!error) {
                try {
                    std::error_code entryError;
                    if (iterator->is_regular_file(entryError) && !entryError && IsDll(iterator->path()) &&
                        !UHI::IsKnownBethesdaGameFile(iterator->path()) &&
                        iterator->file_size(entryError) <= kMaximumDllBytes && !entryError) {
                        candidates.push_back(iterator->path());
                    }
                } catch (...) {
                }
            }
            error.clear();
            iterator.increment(error);
            if (error) error.clear();
        }
        std::ranges::sort(candidates);
        std::vector<std::vector<HotkeyRecord>> perFile(candidates.size());
        std::atomic_size_t next{}, finished{};
        std::mutex progressMutex;
        const auto worker = [&] {
            UHI::EnterBackgroundScanMode();
            while (!ScanCancelled(cancel)) {
                const auto index = next.fetch_add(1);
                if (index >= candidates.size()) break;
                const auto& source = candidates[index];
                {
                    std::scoped_lock lock(progressMutex);
                    ReportScanPath(path, source);
                    ReportItemProgress(itemProgress, source, finished.load(), candidates.size(), 0.0F);
                }
                constexpr std::string_view tag = "dll-v11-bounded-static-functions";
                if (cache) {
                    if (auto cached = cache->Find(tag, source)) {
                        perFile[index] = std::move(*cached);
                        const auto done = finished.fetch_add(1) + 1;
                        std::scoped_lock lock(progressMutex);
                        ReportItemProgress(itemProgress, source, done, candidates.size(), 100.0F);
                        continue;
                    }
                }
                try {
                    float lastReportedFilePercent = 0.0F;
                    const auto fileProgress = [&](const float percent) {
                        const auto clamped = std::clamp(percent, 0.0F, 100.0F);
                        if (clamped < 99.0F && clamped - lastReportedFilePercent < 2.0F) return;
                        lastReportedFilePercent = clamped;
                        std::scoped_lock lock(progressMutex);
                        ReportItemProgress(itemProgress, source, finished.load(), candidates.size(), clamped);
                    };
                    perFile[index] = ScanDll(source, cancel, fileProgress);
                } catch (...) {
                }
                if (cache) cache->Store(tag, source, perFile[index]);
                const auto done = finished.fetch_add(1) + 1;
                {
                    std::scoped_lock lock(progressMutex);
                    ReportItemProgress(itemProgress, source, done, candidates.size(), 100.0F);
                }
            }
        };
        const auto threadCount = std::max<std::size_t>(1, std::min({ workers, candidates.size(), std::size_t{ 4 } }));
        std::vector<std::thread> threads;
        threads.reserve(threadCount);
        for (std::size_t index = 1; index < threadCount; ++index) threads.emplace_back(worker);
        worker();
        for (auto& thread : threads) thread.join();
        for (auto& found : perFile) {
            if (AppendScanResults(records, found)) break;
        }
        return records;
    }
}
