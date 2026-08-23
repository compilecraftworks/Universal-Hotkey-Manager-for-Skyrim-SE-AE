#include "UHI/scanners/BsaScanner.h"

#include "UHI/TextDecoder.h"
#include "UHI/scanners/GenericConfigScanner.h"
#include "UHI/scanners/ControlMapScanner.h"
#include "UHI/scanners/PexScanner.h"
#include "UHI/PathEncoding.h"
#include "UHI/GameFilePolicy.h"
#include "UHI/ScanWorker.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <fstream>
#include <limits>
#include <string>
#include <vector>
#include <atomic>
#include <mutex>
#include <thread>

#include <lz4.h>
#include <zlib.h>

namespace
{
    constexpr std::uint32_t kCompressedToggle = 0x40000000U;
    constexpr std::uint32_t kSizeMask = 0x3FFFFFFFU;
    constexpr std::uint32_t kArchiveCompressed = 0x0004U;
    constexpr std::uint32_t kEmbeddedNames = 0x0100U;
    constexpr std::uint32_t kHasDirectoryNames = 0x0001U;
    constexpr std::uint32_t kHasFileNames = 0x0002U;
    constexpr std::size_t kMaxEntryBytes = 32U * 1024U * 1024U;
    constexpr std::uint32_t kMaxFiles = 1'000'000U;
    constexpr std::uint32_t kMaxFolders = 250'000U;

    std::string Lower(std::string value)
    {
        std::ranges::transform(value, value.begin(), [](const unsigned char character) {
            return static_cast<char>(std::tolower(character));
        });
        return value;
    }

    bool IsBsa(const std::filesystem::path& path) { return Lower(UHI::PathToUtf8(path.extension())) == ".bsa"; }

    bool IsKnownGameArchive(const std::filesystem::path& path)
    {
        return UHI::IsKnownBethesdaGameFile(path);
    }

    bool IsConfigEntry(const std::filesystem::path& path)
    {
        const auto extension = Lower(UHI::PathToUtf8(path.extension()));
        const bool directSettings = extension == ".ini" || extension == ".toml" || extension == ".yaml" ||
            extension == ".yml" || extension == ".cfg" || extension == ".conf" ||
            extension == ".settings" || extension == ".properties";
        if (directSettings) return true;
        const auto value = "/" + Lower(UHI::PathToUtf8(path));
        const bool settingsLocation = value.find("/skse/plugins/") != std::string::npos ||
            value.find("/mcm/") != std::string::npos || value.find("/jcuser/") != std::string::npos ||
            value.find("/configs/") != std::string::npos || value.find("/dllplugins/") != std::string::npos ||
            value.find("/netscriptframework/") != std::string::npos;
        const bool settingsName = value.find("hotkey") != std::string::npos ||
            value.find("keybind") != std::string::npos || value.find("input") != std::string::npos ||
            value.find("control") != std::string::npos || value.find("config") != std::string::npos ||
            value.find("settings") != std::string::npos || value.find("preferences") != std::string::npos;
        if (extension == ".json" || extension == ".jsonc" || extension == ".xml") {
            return settingsLocation || settingsName;
        }
        if (extension != ".txt") return false;
        return settingsLocation || settingsName;
    }

    bool IsNonRuntimeEntry(const std::filesystem::path& path)
    {
        auto value = "/" + Lower(UHI::PathToUtf8(path));
        std::ranges::replace(value, '\\', '/');
        const auto filename = Lower(UHI::PathToUtf8(path.filename()));
        const bool documentationFile = filename == "meta.ini" || filename.ends_with("_kid.ini") ||
            filename == "manifest.json" ||
            filename == "package.json" || filename.starts_with("readme") || filename.starts_with("license") ||
            filename.starts_with("changelog") || filename.starts_with("change-log") || filename.starts_with("credits");
        return documentationFile || filename.find("translation") != std::string::npos ||
            filename.find("localization") != std::string::npos ||
            filename.find("settingstheme") != std::string::npos || value.find("/translations/") != std::string::npos ||
            value.find("/localization/") != std::string::npos || value.find("/themes/") != std::string::npos ||
            value.find("/styles/") != std::string::npos || value.find("/presets/") != std::string::npos ||
            value.find("/backup/") != std::string::npos || value.find("/backups/") != std::string::npos ||
            value.find("/meshes/") != std::string::npos || value.find("/textures/") != std::string::npos ||
            value.find("/sound/") != std::string::npos || value.find("/music/") != std::string::npos ||
            value.find("/video/") != std::string::npos || value.find("/strings/") != std::string::npos ||
            value.find("/shadersfx/") != std::string::npos || value.find("/grass/") != std::string::npos ||
            value.find("/lodsettings/") != std::string::npos || value.find("/facegen/") != std::string::npos ||
            value.find("/screenshots/") != std::string::npos || value.find("/documentation/") != std::string::npos ||
            value.find("/docs/") != std::string::npos || value.find("/licenses/") != std::string::npos ||
            value.find("/readme/") != std::string::npos || value.find("/crashlogs/") != std::string::npos ||
            value.find("/logs/") != std::string::npos || value.find("/animations/") != std::string::npos ||
            value.find("/nemesis_engine/") != std::string::npos || value.find("/calientetools/") != std::string::npos ||
            value.find("/bodyslide/") != std::string::npos || value.find("/dyndolod/") != std::string::npos ||
            value.find("/texgen/") != std::string::npos || value.find("/xlodgen/") != std::string::npos ||
            value.find("/shadercache/") != std::string::npos;
    }

    bool IsScriptEntry(const std::filesystem::path& path)
    {
        const auto extension = Lower(UHI::PathToUtf8(path.extension()));
        return extension == ".pex" || extension == ".psc";
    }

    class Reader
    {
    public:
        explicit Reader(const std::filesystem::path& path) : input_(path, std::ios::binary)
        {
            std::error_code error;
            size_ = std::filesystem::file_size(path, error);
            valid_ = input_.is_open() && !error;
        }

        template <class T>
        bool Read(T& value)
        {
            static_assert(std::is_trivially_copyable_v<T>);
            if (!CanRead(sizeof(T))) return false;
            input_.read(reinterpret_cast<char*>(&value), sizeof(T));
            return static_cast<bool>(input_);
        }

        bool ReadBytes(std::span<char> output)
        {
            if (!CanRead(output.size())) return false;
            input_.read(output.data(), static_cast<std::streamsize>(output.size()));
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
            return position < 0 ? size_ + 1 : static_cast<std::uint64_t>(position);
        }

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

    struct Folder
    {
        std::uint32_t fileCount{};
        std::string name;
    };

    struct Entry
    {
        std::uint32_t sizeFlags{};
        std::uint32_t offset{};
        std::string folder;
        std::string name;
    };

    bool ReadCString(Reader& reader, std::string& value, const std::size_t maxLength = 4096)
    {
        value.clear();
        for (std::size_t index = 0; index < maxLength; ++index) {
            char character{};
            if (!reader.Read(character)) return false;
            if (character == '\0') return true;
            value += character;
        }
        return false;
    }

    bool ReadIndex(Reader& reader, std::uint32_t& version, std::uint32_t& archiveFlags,
        std::vector<Entry>& entries)
    {
        std::array<char, 4> magic{};
        if (!reader.ReadBytes(magic) || magic != std::array<char, 4>{ 'B', 'S', 'A', '\0' } ||
            !reader.Read(version) || (version != 104 && version != 105)) return false;
        std::uint32_t foldersOffset{}, folderCount{}, fileCount{}, folderNamesLength{}, fileNamesLength{}, fileFlags{};
        if (!reader.Read(foldersOffset) || !reader.Read(archiveFlags) || !reader.Read(folderCount) ||
            !reader.Read(fileCount) || !reader.Read(folderNamesLength) || !reader.Read(fileNamesLength) ||
            !reader.Read(fileFlags) || folderCount > kMaxFolders || fileCount > kMaxFiles ||
            (archiveFlags & (kHasDirectoryNames | kHasFileNames)) != (kHasDirectoryNames | kHasFileNames) ||
            !reader.Seek(foldersOffset)) return false;

        std::vector<Folder> folders(folderCount);
        for (auto& folder : folders) {
            std::uint64_t hash{}, offset{};
            std::uint32_t unknown{};
            if (!reader.Read(hash) || !reader.Read(folder.fileCount)) return false;
            if (version == 105) {
                if (!reader.Read(unknown) || !reader.Read(offset)) return false;
            } else {
                std::uint32_t offset32{};
                if (!reader.Read(offset32)) return false;
            }
            if (folder.fileCount > fileCount) return false;
        }

        entries.clear();
        entries.reserve(fileCount);
        for (auto& folder : folders) {
            std::uint8_t nameLength{};
            if (!reader.Read(nameLength) || nameLength == 0 || nameLength > 254) return false;
            std::string folderBytes(nameLength, '\0');
            if (!reader.ReadBytes(folderBytes)) return false;
            if (!folderBytes.empty() && folderBytes.back() == '\0') folderBytes.pop_back();
            folder.name = std::move(folderBytes);
            for (std::uint32_t index = 0; index < folder.fileCount; ++index) {
                std::uint64_t hash{};
                Entry entry;
                entry.folder = folder.name;
                if (!reader.Read(hash) || !reader.Read(entry.sizeFlags) || !reader.Read(entry.offset)) return false;
                entries.push_back(std::move(entry));
            }
        }
        if (entries.size() != fileCount) return false;
        for (auto& entry : entries) {
            if (!ReadCString(reader, entry.name)) return false;
        }
        return true;
    }

    bool Extract(Reader& reader, const std::uint32_t version, const std::uint32_t archiveFlags,
        const Entry& entry, std::vector<char>& output)
    {
        auto storedSize = entry.sizeFlags & kSizeMask;
        const bool toggled = (entry.sizeFlags & kCompressedToggle) != 0;
        const bool compressed = ((archiveFlags & kArchiveCompressed) != 0) != toggled;
        if (storedSize == 0 || !reader.Seek(entry.offset)) return false;
        if ((archiveFlags & kEmbeddedNames) != 0) {
            std::uint8_t nameLength{};
            if (!reader.Read(nameLength) || storedSize < static_cast<std::uint32_t>(nameLength) + 1U) return false;
            std::vector<char> ignored(nameLength);
            if (!reader.ReadBytes(ignored)) return false;
            storedSize -= static_cast<std::uint32_t>(nameLength) + 1U;
        }
        if (!compressed) {
            if (storedSize > kMaxEntryBytes) return false;
            output.resize(storedSize);
            return reader.ReadBytes(output);
        }

        std::uint32_t unpackedSize{};
        if (!reader.Read(unpackedSize) || storedSize < 4 || unpackedSize == 0 || unpackedSize > kMaxEntryBytes) return false;
        storedSize -= 4;
        if (storedSize > kMaxEntryBytes) return false;
        std::vector<char> packed(storedSize);
        if (!reader.ReadBytes(packed)) return false;
        output.assign(unpackedSize, '\0');
        if (version == 105) {
            const auto result = LZ4_decompress_safe(packed.data(), output.data(),
                static_cast<int>(packed.size()), static_cast<int>(output.size()));
            return result == static_cast<int>(output.size());
        }
        uLongf destinationSize = static_cast<uLongf>(output.size());
        return uncompress(reinterpret_cast<Bytef*>(output.data()), &destinationSize,
                   reinterpret_cast<const Bytef*>(packed.data()), static_cast<uLong>(packed.size())) == Z_OK &&
            destinationSize == output.size();
    }

    std::vector<UHI::HotkeyRecord> ScanArchive(const std::filesystem::path& archive,
        const UHI::ScanStage requestedStage, const UHI::CancelCallback& cancel,
        const UHI::PathCallback& path)
    {
        std::vector<UHI::HotkeyRecord> records;
        Reader reader(archive);
        std::uint32_t version{}, flags{};
        std::vector<Entry> entries;
        if (!reader.Valid() || !ReadIndex(reader, version, flags, entries)) return records;
        UHI::Scanners::GenericConfigScanner configScanner;
        UHI::Scanners::PexScanner pexScanner;
        for (const auto& entry : entries) {
            if (UHI::ScanCancelled(cancel) || records.size() >= UHI::kMaximumCollectedRecords) break;
            std::filesystem::path internal = entry.folder;
            internal /= entry.name;
            auto normalized = Lower(UHI::PathToUtf8(internal));
            std::ranges::replace(normalized, '\\', '/');
            const bool includeConfig = requestedStage == UHI::ScanStage::configuration ||
                requestedStage == UHI::ScanStage::runtime;
            const bool includeScripts = requestedStage == UHI::ScanStage::scripts ||
                requestedStage == UHI::ScanStage::runtime;
            if (includeConfig &&
                normalized == "interface/controls/pc/controlmap.txt") continue;
            std::error_code looseError;
            if (std::filesystem::exists(archive.parent_path() / internal, looseError) && !looseError) continue;
            const bool configWanted = includeConfig && IsConfigEntry(internal) && !IsNonRuntimeEntry(internal);
            const bool scriptWanted = includeScripts && IsScriptEntry(internal) && !IsNonRuntimeEntry(internal);
            if (!configWanted && !scriptWanted) continue;
            const auto virtualName = UHI::PathToUtf8(archive) + "::" + UHI::PathToUtf8(internal);
            UHI::ReportScanPath(path, UHI::PathFromUtf8(virtualName));
            std::vector<char> data;
            if (!Extract(reader, version, flags, entry, data)) continue;
            const auto virtualSource = UHI::PathFromUtf8(virtualName);
            std::vector<UHI::HotkeyRecord> found;
            if (configWanted) {
                const std::string_view bytes(data.data(), data.size());
                if (UHI::Scanners::GenericConfigScanner::MayContainBinding(bytes)) {
                    const auto text = UHI::DecodeText(data);
                    found = configScanner.ScanContent(virtualSource, text, UHI::PathToUtf8(archive.stem()),
                        UHI::NumericCodeSpace::skseUnifiedInputCode);
                }
            } else if (scriptWanted) {
                const std::string_view bytes(data.data(), data.size());
                if (UHI::Scanners::PexScanner::MayContainInputMarker(bytes)) {
                    found = pexScanner.ScanContent(virtualSource, bytes);
                }
            }
            if (UHI::AppendScanResults(records, found)) break;
        }
        return records;
    }
}

namespace UHI::Scanners
{
    std::optional<std::string> BsaScanner::ExtractText(const std::filesystem::path& archive,
        const std::filesystem::path& internalPath) const noexcept
    {
        // Bethesda base-game, DLC, bundled Creation Club/AE and Creations
        // resource archives are never opened. Known game controls come from
        // UHI's built-in table; only a loose controlmap is parsed.
        if (IsKnownGameArchive(archive)) return std::nullopt;
        try {
            Reader reader(archive);
            std::uint32_t version{}, flags{};
            std::vector<Entry> entries;
            if (!reader.Valid() || !ReadIndex(reader, version, flags, entries)) return std::nullopt;
            auto wanted = Lower(UHI::PathToUtf8(internalPath));
            std::ranges::replace(wanted, '\\', '/');
            for (const auto& entry : entries) {
                std::filesystem::path candidate = entry.folder;
                candidate /= entry.name;
                auto normalized = Lower(UHI::PathToUtf8(candidate));
                std::ranges::replace(normalized, '\\', '/');
                if (normalized != wanted) continue;
                std::vector<char> data;
                if (!Extract(reader, version, flags, entry, data)) return std::nullopt;
                return UHI::DecodeText(data);
            }
        } catch (...) {
        }
        return std::nullopt;
    }

    std::vector<HotkeyRecord> BsaScanner::Scan(const std::filesystem::path& root,
        const ScanStage requestedStage, const CancelCallback& cancel, const PathCallback& path,
        ScanCache* cache, const std::size_t workers, const ItemProgressCallback& itemProgress) const noexcept
    {
        std::vector<HotkeyRecord> records;
        if (requestedStage != ScanStage::configuration && requestedStage != ScanStage::scripts &&
            requestedStage != ScanStage::runtime) return records;
        try {
            std::error_code error;
            if (!std::filesystem::exists(root, error)) return records;
            std::vector<std::filesystem::path> candidates;
            std::filesystem::directory_iterator iterator(root,
                std::filesystem::directory_options::skip_permission_denied, error), end;
            while (iterator != end) {
                if (ScanCancelled(cancel)) break;
                if (!error) {
                    try {
                        std::error_code entryError;
                        if (iterator->is_regular_file(entryError) && !entryError && IsBsa(iterator->path()) &&
                            !IsKnownGameArchive(iterator->path())) candidates.push_back(iterator->path());
                    } catch (...) {}
                }
                error.clear();
                iterator.increment(error);
                if (error) error.clear();
            }
            std::ranges::sort(candidates);
            std::vector<std::vector<HotkeyRecord>> perArchive(candidates.size());
            std::atomic_size_t next{}, finished{};
            std::mutex progressMutex;
            const auto worker = [&] {
                UHI::EnterBackgroundScanMode();
                while (!ScanCancelled(cancel)) {
                    const auto index = next.fetch_add(1);
                    if (index >= candidates.size()) break;
                    const auto& archive = candidates[index];
                    {
                        std::scoped_lock lock(progressMutex);
                        ReportScanPath(path, archive);
                        ReportItemProgress(itemProgress, archive, finished.load(), candidates.size(), 0.0F);
                    }
                    const auto tag = requestedStage == ScanStage::configuration ? "bsa-config-v9-owner-action" :
                        requestedStage == ScanStage::scripts ? "bsa-pex-v7-mcm-dataflow" : "bsa-all-v3-active";
                    if (cache) {
                        if (auto cached = cache->Find(tag, archive)) {
                            perArchive[index] = std::move(*cached);
                            const auto done = finished.fetch_add(1) + 1;
                            std::scoped_lock lock(progressMutex);
                            ReportItemProgress(itemProgress, archive, done, candidates.size(), 100.0F);
                            continue;
                        }
                    }
                    try {
                        const auto synchronizedPath = [&](const std::filesystem::path& current) {
                            std::scoped_lock lock(progressMutex);
                            ReportScanPath(path, current);
                            ReportItemProgress(itemProgress, current, finished.load(), candidates.size(), 50.0F);
                        };
                        perArchive[index] = ScanArchive(archive, requestedStage, cancel, synchronizedPath);
                    } catch (...) {
                        // A malformed archive is isolated to this file.
                    }
                    if (cache) cache->Store(tag, archive, perArchive[index]);
                    const auto done = finished.fetch_add(1) + 1;
                    {
                        std::scoped_lock lock(progressMutex);
                        ReportItemProgress(itemProgress, archive, done, candidates.size(), 100.0F);
                    }
                }
            };
            const auto threadCount = std::max<std::size_t>(1, std::min({ workers, candidates.size(), std::size_t{ 4 } }));
            std::vector<std::thread> threads;
            threads.reserve(threadCount);
            for (std::size_t index = 1; index < threadCount; ++index) threads.emplace_back(worker);
            worker();
            for (auto& thread : threads) thread.join();
            for (auto& found : perArchive) {
                if (AppendScanResults(records, found)) break;
            }
        } catch (...) {
        }
        return records;
    }

    std::vector<HotkeyRecord> BsaScanner::ScanAll(const std::filesystem::path& root,
        const CancelCallback& cancel, const PathCallback& path, ScanCache* cache,
        const std::size_t workers, const ItemProgressCallback& itemProgress) const noexcept
    {
        // runtime is used internally as a combined selector only. Individual
        // records are still emitted as configuration or scripts.
        return Scan(root, ScanStage::runtime, cancel, path, cache, workers, itemProgress);
    }
}
