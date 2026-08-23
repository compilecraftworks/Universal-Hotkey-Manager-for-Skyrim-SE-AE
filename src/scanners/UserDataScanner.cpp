#include "UHI/scanners/UserDataScanner.h"
#include "UHI/PathEncoding.h"

#include "UHI/ConfigBindingParser.h"
#include "UHI/TextDecoder.h"
#include "UHI/scanners/GenericConfigScanner.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <optional>
#include <span>
#include <sstream>
#include <string>
#include <unordered_set>
#include <vector>

#ifdef _WIN32
#include <Windows.h>
#include <ShlObj.h>
#endif

namespace
{
    constexpr std::uint32_t kMaximumPlugins = 4096U;
    constexpr std::uint32_t kMaximumChunks = 1'000'000U;
    constexpr std::uint32_t kMaximumRegistrations = 1'000'000U;
    constexpr std::uint32_t kMaximumInspectedChunk = 16U * 1024U * 1024U;
    constexpr std::uintmax_t kMaximumExternalFile = 16U * 1024U * 1024U;
    constexpr std::size_t kMaximumCandidateRecords = 4096U;

    std::string Lower(std::string value)
    {
        std::ranges::transform(value, value.begin(), [](const unsigned char character) {
            return static_cast<char>(std::tolower(character));
        });
        return value;
    }

    bool IsTag(const std::uint32_t value, const std::string_view tag)
    {
        if (tag.size() != 4) return false;
        const auto be = (static_cast<std::uint32_t>(static_cast<unsigned char>(tag[0])) << 24U) |
            (static_cast<std::uint32_t>(static_cast<unsigned char>(tag[1])) << 16U) |
            (static_cast<std::uint32_t>(static_cast<unsigned char>(tag[2])) << 8U) |
            static_cast<unsigned char>(tag[3]);
        const auto le = static_cast<std::uint32_t>(static_cast<unsigned char>(tag[0])) |
            (static_cast<std::uint32_t>(static_cast<unsigned char>(tag[1])) << 8U) |
            (static_cast<std::uint32_t>(static_cast<unsigned char>(tag[2])) << 16U) |
            (static_cast<std::uint32_t>(static_cast<unsigned char>(tag[3])) << 24U);
        return value == be || value == le;
    }

    std::string FourCC(const std::uint32_t value)
    {
        std::array<char, 4> high{
            static_cast<char>(value >> 24U), static_cast<char>(value >> 16U),
            static_cast<char>(value >> 8U), static_cast<char>(value)
        };
        const auto printable = [](const auto& characters) {
            return std::ranges::all_of(characters, [](const unsigned char character) {
                return character >= 0x20 && character <= 0x7E;
            });
        };
        if (printable(high)) return std::string(high.begin(), high.end());
        std::array<char, 4> low{ high[3], high[2], high[1], high[0] };
        if (printable(low)) return std::string(low.begin(), low.end());
        std::ostringstream stream;
        stream << "0x" << std::hex << std::uppercase << std::setw(8) << std::setfill('0') << value;
        return stream.str();
    }

    class FileReader
    {
    public:
        explicit FileReader(const std::filesystem::path& path) : input_(path, std::ios::binary)
        {
            std::error_code error;
            size_ = std::filesystem::file_size(path, error);
            valid_ = input_.is_open() && !error;
        }

        template <class T>
        bool Read(T& value)
        {
            if (!CanRead(sizeof(T))) return false;
            input_.read(reinterpret_cast<char*>(&value), sizeof(T));
            return static_cast<bool>(input_);
        }

        bool ReadBytes(std::vector<char>& bytes, const std::size_t count)
        {
            if (!CanRead(count)) return false;
            bytes.resize(count);
            input_.read(bytes.data(), static_cast<std::streamsize>(count));
            return static_cast<bool>(input_);
        }

        bool Seek(const std::uint64_t position)
        {
            if (!valid_ || position > size_) return false;
            input_.clear();
            input_.seekg(static_cast<std::streamoff>(position), std::ios::beg);
            return static_cast<bool>(input_);
        }

        std::uint64_t Position()
        {
            const auto position = input_.tellg();
            return position < 0 ? size_ + 1U : static_cast<std::uint64_t>(position);
        }

        std::uint64_t Size() const noexcept { return size_; }
        bool Valid() const noexcept { return valid_; }

    private:
        bool CanRead(const std::size_t count)
        {
            const auto position = Position();
            return valid_ && position <= size_ && count <= size_ - position;
        }

        std::ifstream input_;
        std::uint64_t size_{};
        bool valid_{};
    };

    std::filesystem::path DocumentsFolder()
    {
#ifdef _WIN32
        PWSTR raw{};
        if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Documents, KF_FLAG_DONT_VERIFY, nullptr, &raw)) && raw) {
            std::filesystem::path result(raw);
            CoTaskMemFree(raw);
            return result;
        }
        std::array<wchar_t, 32768> profile{};
        const auto length = GetEnvironmentVariableW(L"USERPROFILE", profile.data(),
            static_cast<DWORD>(profile.size()));
        if (length > 0 && length < profile.size()) return std::filesystem::path(profile.data()) / "Documents";
#endif
        return {};
    }

    bool IsExternalData(const std::filesystem::path& path)
    {
        const auto extension = Lower(UHI::PathToUtf8(path.extension()));
        return extension == ".json" || extension == ".jsonc" || extension == ".txt" || extension == ".ini" ||
            extension == ".toml" || extension == ".yaml" || extension == ".yml" || extension == ".xml" ||
            extension == ".cfg" || extension == ".conf" || extension == ".settings" || extension == ".properties";
    }

    void ScanExternalRoot(const std::filesystem::path& root, const UHI::CancelCallback& cancel,
        const UHI::PathCallback& path, std::vector<UHI::HotkeyRecord>& records)
    {
        std::error_code error;
        if (!std::filesystem::exists(root, error)) return;
        UHI::Scanners::GenericConfigScanner scanner;
        for (const auto& entry : std::filesystem::recursive_directory_iterator(root,
                 std::filesystem::directory_options::skip_permission_denied, error)) {
            if (UHI::ScanCancelled(cancel) || records.size() >= UHI::kMaximumCollectedRecords) break;
            if (error) { error.clear(); continue; }
            try {
                std::error_code entryError;
                if (!entry.is_regular_file(entryError) || entryError || !IsExternalData(entry.path()) ||
                    entry.file_size(entryError) > kMaximumExternalFile || entryError) continue;
                UHI::ReportScanPath(path, entry.path());
                std::ifstream input(entry.path(), std::ios::binary);
                if (!input) continue;
                const std::string bytes((std::istreambuf_iterator<char>(input)), {});
                const auto text = UHI::DecodeText(std::span<const char>(bytes.data(), bytes.size()));
                // JCUser/Papyrus-persisted MCM key-map values use the SKSE
                // unified range, including mouse 256..265 and pad 266..281.
                auto found = scanner.ScanContent(entry.path(), text, {},
                    UHI::NumericCodeSpace::skseUnifiedInputCode);
                for (auto& record : found) {
                    record.detector = "PersistentSettingsScanner";
                    record.stage = UHI::ScanStage::runtime;
                }
                if (UHI::AppendScanResults(records, found)) break;
            } catch (...) {
            }
        }
    }

    bool InterestingToken(const std::string& token)
    {
        const auto lowered = Lower(token);
        return lowered.find("hotkey") != std::string::npos || lowered.find("keybind") != std::string::npos ||
            lowered.find("keycode") != std::string::npos || lowered.find("shortcut") != std::string::npos ||
            lowered.find("keymap") != std::string::npos || lowered.find("_key") != std::string::npos ||
            lowered.find(".key") != std::string::npos;
    }

    void AddBinaryCandidates(const std::filesystem::path& source, const std::string_view owner,
        const std::vector<char>& data, std::vector<UHI::HotkeyRecord>& records,
        std::unordered_set<std::string>& seen)
    {
        for (std::size_t offset = 0; offset < data.size() && records.size() < kMaximumCandidateRecords;) {
            while (offset < data.size() && (static_cast<unsigned char>(data[offset]) < 0x20 ||
                                               static_cast<unsigned char>(data[offset]) > 0x7E)) ++offset;
            const auto begin = offset;
            while (offset < data.size() && static_cast<unsigned char>(data[offset]) >= 0x20 &&
                static_cast<unsigned char>(data[offset]) <= 0x7E && offset - begin < 192U) ++offset;
            if (offset - begin < 4U) continue;
            const std::string token(data.data() + begin, data.data() + offset);
            if (!InterestingToken(token)) continue;
            const auto searchEnd = (std::min)(data.size(), offset + 68U);
            for (std::size_t candidateOffset = offset; candidateOffset + 4U <= searchEnd; ++candidateOffset) {
                std::uint32_t code{};
                std::memcpy(&code, data.data() + candidateOffset, sizeof(code));
                if (code == token.size() || code == 0 || code >= 282) continue;
                const auto parsed = UHI::ParseSkseInputCode(std::to_string(code));
                if (!parsed.conflictEligible) continue;
                const auto identity = std::string(owner) + '\x1F' + token + '\x1F' + std::to_string(code);
                if (!seen.insert(identity).second) break;
                records.push_back({
                    .owner = std::string(owner), .action = "Saved candidate: " + token,
                    .binding = parsed.binding, .rawBinding = std::to_string(code), .codeSystem = parsed.codeSystem,
                    .device = parsed.device,
                    .detector = "CoSaveHeuristicScanner", .confidence = UHI::Confidence::candidate,
                    .evidencePath = source, .evidenceLine = 0, .stage = UHI::ScanStage::runtime,
                    .conflictEligible = false
                });
                break;
            }
        }
    }

    void InspectChunk(const std::filesystem::path& source, const std::uint32_t pluginSignature,
        const std::uint32_t chunkType, const std::vector<char>& data, bool& readingKeyRegistrations,
        std::vector<UHI::HotkeyRecord>& records, std::unordered_set<std::string>& seen)
    {
        if (IsTag(chunkType, "KEYR")) {
            readingKeyRegistrations = true;
            return;
        }
        if (readingKeyRegistrations && IsTag(chunkType, "REGE")) {
            readingKeyRegistrations = false;
            return;
        }
        if (readingKeyRegistrations && IsTag(chunkType, "REGS") && data.size() >= 8U) {
            std::uint32_t key{}, count{};
            std::memcpy(&key, data.data(), sizeof(key));
            std::memcpy(&count, data.data() + 4, sizeof(count));
            if (key > 0 && key < 282 && count <= kMaximumRegistrations &&
                static_cast<std::uint64_t>(count) * 8U <= data.size() - 8U) {
                const auto parsed = UHI::ParseSkseInputCode(std::to_string(key));
                for (std::uint32_t index = 0; index < count && records.size() < kMaximumCandidateRecords; ++index) {
                    std::uint64_t handle{};
                    std::memcpy(&handle, data.data() + 8U + static_cast<std::size_t>(index) * 8U, sizeof(handle));
                    std::ostringstream owner;
                    owner << "Papyrus handle 0x" << std::hex << std::uppercase << handle;
                    const auto identity = owner.str() + '\x1F' + std::to_string(key);
                    if (!seen.insert(identity).second) continue;
                    records.push_back({
                        .owner = owner.str(), .action = "Saved SKSE RegisterForKey registration",
                        .binding = parsed.binding, .rawBinding = std::to_string(key), .codeSystem = parsed.codeSystem,
                        .device = parsed.device,
                        .detector = "SkseCoSaveKeyRegistry", .confidence = UHI::Confidence::inferred,
                        .evidencePath = source, .evidenceLine = 0, .stage = UHI::ScanStage::runtime,
                        .conflictEligible = parsed.conflictEligible
                    });
                }
            }
            return;
        }

        UHI::Scanners::GenericConfigScanner scanner;
        auto structured = scanner.ScanContent(source,
            std::string_view(data.data(), data.size()), FourCC(pluginSignature));
        for (auto& record : structured) {
            record.detector = "CoSaveStructuredScanner";
            record.stage = UHI::ScanStage::runtime;
            record.editable = false;
        }
        UHI::AppendScanResults(records, structured, kMaximumCandidateRecords);
        AddBinaryCandidates(source, FourCC(pluginSignature), data, records, seen);
    }

    std::vector<UHI::HotkeyRecord> ScanCoSave(const std::filesystem::path& source,
        const UHI::CancelCallback& cancel, const UHI::PathCallback& path)
    {
        std::vector<UHI::HotkeyRecord> records;
        UHI::ReportScanPath(path, source);
        FileReader reader(source);
        std::array<char, 4> signature{};
        std::uint32_t format{}, skse{}, runtime{}, pluginCount{};
        if (!reader.Valid() || !reader.Read(signature) || signature != std::array<char, 4>{ 'S', 'K', 'S', 'E' } ||
            !reader.Read(format) || format == 0 || !reader.Read(skse) || !reader.Read(runtime) ||
            !reader.Read(pluginCount) || pluginCount > kMaximumPlugins) return records;
        std::unordered_set<std::string> seen;
        for (std::uint32_t pluginIndex = 0; pluginIndex < pluginCount && !UHI::ScanCancelled(cancel); ++pluginIndex) {
            std::uint32_t pluginSignature{}, chunkCount{}, pluginLength{};
            if (!reader.Read(pluginSignature) || !reader.Read(chunkCount) || !reader.Read(pluginLength) ||
                chunkCount > kMaximumChunks) return {};
            const auto pluginBegin = reader.Position();
            if (pluginBegin > reader.Size() || pluginLength > reader.Size() - pluginBegin) return {};
            const auto pluginEnd = pluginBegin + pluginLength;
            bool keyRegistrations{};
            for (std::uint32_t chunkIndex = 0; chunkIndex < chunkCount && !UHI::ScanCancelled(cancel); ++chunkIndex) {
                if (reader.Position() > pluginEnd || pluginEnd - reader.Position() < 12U) return {};
                std::uint32_t type{}, version{}, length{};
                if (!reader.Read(type) || !reader.Read(version) || !reader.Read(length) ||
                    length > pluginEnd - reader.Position()) return {};
                if (length <= kMaximumInspectedChunk) {
                    std::vector<char> data;
                    if (!reader.ReadBytes(data, length)) return {};
                    InspectChunk(source, pluginSignature, type, data, keyRegistrations, records, seen);
                } else if (!reader.Seek(reader.Position() + length)) {
                    return {};
                }
            }
            if (!reader.Seek(pluginEnd)) return {};
        }
        return records;
    }

    std::optional<std::filesystem::path> SelectCoSave(const std::vector<std::filesystem::path>& saveRoots,
        const std::string_view preferredSaveName, const UHI::CancelCallback& cancel)
    {
        std::vector<std::filesystem::path> candidates;
        for (const auto& root : saveRoots) {
            std::error_code error;
            if (!std::filesystem::exists(root, error)) continue;
            for (const auto& entry : std::filesystem::recursive_directory_iterator(root,
                     std::filesystem::directory_options::skip_permission_denied, error)) {
                if (UHI::ScanCancelled(cancel)) return std::nullopt;
                if (error) { error.clear(); continue; }
                std::error_code entryError;
                if (entry.is_regular_file(entryError) && !entryError &&
                    Lower(UHI::PathToUtf8(entry.path().extension())) == ".skse") candidates.push_back(entry.path());
                if (candidates.size() >= 10000U) break;
            }
        }
        if (candidates.empty()) return std::nullopt;
        auto preferred = Lower(UHI::PathToUtf8(std::filesystem::path(preferredSaveName).stem()));
        if (!preferred.empty()) {
            std::erase_if(candidates, [&](const auto& path) {
                return Lower(UHI::PathToUtf8(path.stem())) != preferred;
            });
            if (candidates.empty()) return std::nullopt;
        }
        std::ranges::sort(candidates, [](const auto& left, const auto& right) {
            std::error_code leftError, rightError;
            const auto leftTime = std::filesystem::last_write_time(left, leftError);
            const auto rightTime = std::filesystem::last_write_time(right, rightError);
            if (leftError) return false;
            if (rightError) return true;
            return leftTime > rightTime;
        });
        return candidates.front();
    }
}

namespace UHI::Scanners
{
    std::vector<HotkeyRecord> UserDataScanner::Scan(const std::filesystem::path& gameRoot,
        const std::string_view preferredSaveName, const CancelCallback& cancel,
        const std::filesystem::path& documentsOverride, const PathCallback& path) const noexcept
    {
        std::vector<HotkeyRecord> records;
        try {
            const auto documents = documentsOverride.empty() ? DocumentsFolder() : documentsOverride;
            if (documents.empty()) return records;
            const std::array gameFolders{
                std::filesystem::path("Skyrim Special Edition"),
                std::filesystem::path("Skyrim Special Edition GOG"),
                std::filesystem::path("Skyrim VR")
            };
            std::vector<std::filesystem::path> saveRoots;
            for (const auto& gameFolder : gameFolders) {
                if (records.size() >= kMaximumCollectedRecords || ScanCancelled(cancel)) break;
                const auto base = documents / "My Games" / gameFolder;
                ScanExternalRoot(base / "JCUser", cancel, path, records);
                ScanExternalRoot(base / "SKSE", cancel, path, records);
                saveRoots.push_back(base / "Saves");
            }
            // Some launchers expose a local/profile save path relative to the
            // game root. Include it without assuming any MO2 installation path.
            saveRoots.push_back(gameRoot / "Saves");
            if (const auto selected = SelectCoSave(saveRoots, preferredSaveName, cancel)) {
                auto saved = ScanCoSave(*selected, cancel, path);
                AppendScanResults(records, saved);
            }
        } catch (...) {
            // User folders may disappear or be virtualized while scanning.
        }
        return records;
    }
}
