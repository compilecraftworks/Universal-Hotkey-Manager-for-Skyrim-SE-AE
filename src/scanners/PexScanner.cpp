#include "UHI/scanners/PexScanner.h"
#include "UHI/PathEncoding.h"
#include "UHI/ConfigBindingParser.h"
#include "UHI/scanners/PexBytecodeScanner.h"
#include "UHI/GameFilePolicy.h"
#include "UHI/ScanWorker.h"
#include "UHI/ActivationContextInference.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <regex>
#include <string>
#include <array>
#include <atomic>
#include <mutex>
#include <thread>

namespace
{
    constexpr std::uintmax_t kMaximumScriptBytes = 16U * 1024U * 1024U;
    std::string Lower(std::string value)
    {
        std::ranges::transform(value, value.begin(), [](const unsigned char character) {
            return static_cast<char>(std::tolower(character));
        });
        return value;
    }

    bool IsScript(const std::filesystem::path& path)
    {
        auto extension = UHI::PathToUtf8(path.extension());
        std::ranges::transform(extension, extension.begin(), [](const unsigned char character) {
            return static_cast<char>(std::tolower(character));
        });
        return extension == ".pex" || extension == ".psc";
    }

    bool IsInactiveScriptCopy(const std::filesystem::path& path)
    {
        auto value = Lower(UHI::PathToUtf8(path));
        std::ranges::replace(value, '\\', '/');
        return value.find("/backup/") != std::string::npos ||
            value.find("/backups/") != std::string::npos;
    }

    bool IsInteresting(std::string value)
    {
        std::ranges::transform(value, value.begin(), [](const unsigned char c) {
            return static_cast<char>(std::tolower(c));
        });
        return value.find("hotkey") != std::string::npos || value.find("keybind") != std::string::npos ||
               value.find("shortcut") != std::string::npos || value.find("registerforkey") != std::string::npos ||
               value.find("registerforcontrol") != std::string::npos || value.find("oncontroldown") != std::string::npos ||
               value.find("oncontrolup") != std::string::npos;
    }

    bool ContainsAsciiInsensitive(const std::string_view bytes, const std::string_view needle)
    {
        if (needle.empty() || bytes.size() < needle.size()) return false;
        const auto equal = [](const char left, const char right) {
            return std::tolower(static_cast<unsigned char>(left)) ==
                std::tolower(static_cast<unsigned char>(right));
        };
        return std::search(bytes.begin(), bytes.end(), needle.begin(), needle.end(), equal) != bytes.end();
    }

    void AddKeyRegistration(std::vector<UHI::HotkeyRecord>& records,
        const std::filesystem::path& path, const std::string& token)
    {
        if (records.size() >= UHI::kMaximumCollectedRecords) return;
        // Some source-built/loosely packed PEX files retain a readable call
        // expression. When it does, a DX scan code next to RegisterForKey is
        // far more useful than an opaque string-table hint.
        static const std::regex registration(
            R"((?:registerforkey|onkeydown|onkeyup)\s*\(?\s*[:=,]?\s*([0-9]{1,3}))",
            std::regex::icase);
        std::smatch match;
        if (!std::regex_search(token, match, registration)) {
            return;
        }
        const auto raw = match[1].str();
        const auto parsed = UHI::ParseSkseInputCode(raw);
        const auto owner = UHI::PathToUtf8(path.stem());
        records.push_back({
            .owner = owner,
            // A readable RegisterForKey call proves the key, but not the
            // branch that handles it. The script/mod name is the honest
            // semantic fallback when bytecode metadata has no MCM label.
            .action = owner,
            .binding = parsed.binding,
            .rawBinding = raw,
            .codeSystem = parsed.codeSystem,
            .device = parsed.device,
            .detector = "PexScanner",
            .confidence = parsed.conflictEligible ? UHI::Confidence::inferred : UHI::Confidence::candidate,
            .evidencePath = path,
            .evidenceLine = 0,
            .stage = UHI::ScanStage::scripts,
            .conflictEligible = parsed.conflictEligible
        });
    }
}

namespace UHI::Scanners
{
    bool PexScanner::MayContainInputMarker(const std::string_view bytes) noexcept
    {
        try {
            static constexpr std::array<std::string_view, 19> markers{
                "RegisterForKey", "UnregisterForKey", "OnKeyDown", "OnKeyUp", "IsKeyPressed",
                "GetMappedKey", "AddKeyMapOption", "SetKeyMapOption", "StartRemapMode",
                "RegisterForControl", "UnregisterForControl", "OnControlDown", "OnControlUp",
                "TapKey", "HoldKey", "ReleaseKey", "hotkey", "keybind", "shortcut"
            };
            return std::ranges::any_of(markers, [&](const auto marker) {
                return ContainsAsciiInsensitive(bytes, marker);
            });
        } catch (...) {
            return true;
        }
    }

    std::vector<HotkeyRecord> PexScanner::ScanContent(const std::filesystem::path& source,
        const std::string_view byteView) const
    {
        if (byteView.size() > kMaximumScriptBytes || !MayContainInputMarker(byteView)) return {};
        auto records = PexBytecodeScanner{}.ScanContent(source, byteView);
        const auto fileActivation = InferActivationContext(
            byteView, ContextEvidenceSource::papyrusFunction);
        const std::string bytes(byteView);
        for (std::size_t offset = 0; offset < bytes.size();) {
            if (records.size() >= kMaximumCollectedRecords) break;
            while (offset < bytes.size() && (static_cast<unsigned char>(bytes[offset]) < 0x20 ||
                                               static_cast<unsigned char>(bytes[offset]) > 0x7E)) {
                ++offset;
            }
            const auto start = offset;
            while (offset < bytes.size() && static_cast<unsigned char>(bytes[offset]) >= 0x20 &&
                   static_cast<unsigned char>(bytes[offset]) <= 0x7E) {
                ++offset;
            }
            if (offset - start < 4) continue;
            const auto token = bytes.substr(start, offset - start);
            AddKeyRegistration(records, source, token);
            if (records.size() >= kMaximumCollectedRecords) break;
            if (!IsInteresting(token)) continue;
            records.push_back({
                .owner = UHI::PathToUtf8(source.stem()),
                .action = "Input registration hint",
                .binding = "PEX string: " + token,
                .rawBinding = token,
                .codeSystem = "PEX string evidence (no numeric code decoded)",
                .device = "unknown",
                .detector = "PexScanner",
                .confidence = Confidence::candidate,
                .evidencePath = source,
                .evidenceLine = 0,
                .stage = ScanStage::scripts,
                .conflictEligible = false
            });
        }
        if (fileActivation.mask != 0) {
            for (auto& record : records) {
                if (record.contextMask != 0) continue;
                record.contextMask = fileActivation.mask;
                record.contextConfidence = fileActivation.confidence;
            }
        }
        // PSC is source/reference material and is never executed by Skyrim.
        // Keep decoded evidence in the raw report, but do not present it as an
        // active physical shortcut when only the source copy exists.
        if (Lower(UHI::PathToUtf8(source.extension())) == ".psc") {
            for (auto& record : records) record.conflictEligible = false;
        }
        return records;
    }

    std::vector<HotkeyRecord> PexScanner::Scan(const std::filesystem::path& root,
        const CancelCallback& cancel, const PathCallback& path, ScanCache* cache,
        const std::size_t workers, const ItemProgressCallback& itemProgress) const
    {
        std::vector<HotkeyRecord> records;
        std::error_code error;
        if (!std::filesystem::exists(root, error)) {
            return records;
        }

        std::vector<std::filesystem::path> candidates;
        std::filesystem::recursive_directory_iterator iterator(root,
            std::filesystem::directory_options::skip_permission_denied, error), end;
        while (iterator != end) {
            if (ScanCancelled(cancel)) break;
            if (!error) {
                try {
                    std::error_code entryError;
                    if (iterator->is_regular_file(entryError) && !entryError && IsScript(iterator->path()) &&
                        !IsInactiveScriptCopy(iterator->path()) &&
                        !UHI::IsKnownBethesdaGameFile(iterator->path()) &&
                        iterator->file_size(entryError) <= kMaximumScriptBytes && !entryError) {
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
                // Bump whenever bytecode data-flow or MCM option recovery
                // changes.  Older cached PEX records do not contain the
                // setting identities required for live MCM resolution.
                constexpr std::string_view tag = "pex-v9-mcm-dataflow";
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
                    std::ifstream input(source, std::ios::binary);
                    if (input) {
                        std::string bytes((std::istreambuf_iterator<char>(input)), {});
                        {
                            std::scoped_lock lock(progressMutex);
                            ReportItemProgress(itemProgress, source, finished.load(), candidates.size(), 40.0F);
                        }
                        perFile[index] = ScanContent(source, bytes);
                        {
                            std::scoped_lock lock(progressMutex);
                            ReportItemProgress(itemProgress, source, finished.load(), candidates.size(), 95.0F);
                        }
                    }
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
