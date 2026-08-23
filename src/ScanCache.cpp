#include "UHI/ScanCache.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <fstream>
#include <limits>
#include <span>
#include <system_error>
#ifdef _WIN32
#include <Windows.h>
#endif

namespace
{
    constexpr std::array<char, 8> kMagic{ 'U', 'H', 'I', 'C', 'A', 'C', 'H', 'E' };
    constexpr std::size_t kMaximumEntries = 100'000;
    constexpr std::size_t kMaximumRecordsPerEntry = 4096;
    constexpr std::size_t kMaximumStringBytes = 1U * 1024U * 1024U;
    constexpr std::uintmax_t kMaximumCacheBytes = 256U * 1024U * 1024U;

    template <class T>
    bool Read(std::istream& input, T& value)
    {
        static_assert(std::is_trivially_copyable_v<T>);
        input.read(reinterpret_cast<char*>(&value), sizeof(value));
        return static_cast<bool>(input);
    }

    template <class T>
    void Write(std::ostream& output, const T& value)
    {
        static_assert(std::is_trivially_copyable_v<T>);
        output.write(reinterpret_cast<const char*>(&value), sizeof(value));
    }

    bool ReadString(std::istream& input, std::string& value)
    {
        std::uint32_t size{};
        if (!Read(input, size) || size > kMaximumStringBytes) return false;
        value.resize(size);
        input.read(value.data(), static_cast<std::streamsize>(size));
        return static_cast<bool>(input);
    }

    void WriteString(std::ostream& output, const std::string_view value)
    {
        const auto size = static_cast<std::uint32_t>(value.size());
        Write(output, size);
        output.write(value.data(), static_cast<std::streamsize>(value.size()));
    }

    std::string Utf8(const std::filesystem::path& path)
    {
        const auto value = path.generic_u8string();
        return std::string(reinterpret_cast<const char*>(value.data()), value.size());
    }

    std::filesystem::path FromUtf8(const std::string& value)
    {
        return std::filesystem::path(std::u8string(
            reinterpret_cast<const char8_t*>(value.data()), value.size()));
    }

    void WriteRecord(std::ostream& output, const UHI::HotkeyRecord& value)
    {
        WriteString(output, value.owner); WriteString(output, value.action); WriteString(output, value.binding);
        WriteString(output, value.rawBinding); WriteString(output, value.settingName); WriteString(output, value.settingSection);
        WriteString(output, value.codeSystem); WriteString(output, value.device);
        WriteString(output, value.detector); WriteString(output, value.categoryHint);
        WriteString(output, Utf8(value.evidencePath));
        Write(output, static_cast<std::uint8_t>(value.confidence));
        Write(output, static_cast<std::uint8_t>(value.stage));
        Write(output, static_cast<std::uint64_t>(value.evidenceLine));
        Write(output, static_cast<std::uint8_t>(value.editable));
        Write(output, static_cast<std::uint8_t>(value.runtimeActive));
        Write(output, static_cast<std::uint8_t>(value.conflictEligible));
        Write(output, static_cast<std::uint8_t>(value.uiLocalOnly));
        Write(output, value.contextMask);
        Write(output, static_cast<std::uint8_t>(value.contextConfidence));
    }

    bool ReadRecord(std::istream& input, UHI::HotkeyRecord& value)
    {
        std::string path;
        if (!ReadString(input, value.owner) || !ReadString(input, value.action) ||
            !ReadString(input, value.binding) || !ReadString(input, value.rawBinding) ||
            !ReadString(input, value.settingName) || !ReadString(input, value.settingSection) ||
            !ReadString(input, value.codeSystem) || !ReadString(input, value.device) ||
            !ReadString(input, value.detector) || !ReadString(input, value.categoryHint) ||
            !ReadString(input, path)) return false;
        std::uint8_t confidence{}, stage{}, editable{}, runtimeActive{}, eligible{}, uiLocalOnly{}, contextConfidence{};
        std::uint32_t contextMask{};
        std::uint64_t line{};
        if (!Read(input, confidence) || !Read(input, stage) || !Read(input, line) ||
            !Read(input, editable) || !Read(input, runtimeActive) || !Read(input, eligible) ||
            !Read(input, uiLocalOnly) || !Read(input, contextMask) ||
            !Read(input, contextConfidence) || confidence > 2 || stage > 3 ||
            editable > 1 || runtimeActive > 1 || eligible > 1 || uiLocalOnly > 1 || contextConfidence > 2) return false;
        value.confidence = static_cast<UHI::Confidence>(confidence);
        value.stage = static_cast<UHI::ScanStage>(stage);
        value.evidenceLine = static_cast<std::size_t>(line);
        value.editable = editable != 0;
        value.runtimeActive = runtimeActive != 0;
        value.conflictEligible = eligible != 0;
        value.uiLocalOnly = uiLocalOnly != 0;
        value.contextMask = contextMask;
        value.contextConfidence = static_cast<UHI::ContextConfidence>(contextConfidence);
        value.evidencePath = FromUtf8(path);
        return true;
    }
}

namespace UHI
{
    ScanCache::ScanCache(std::filesystem::path path) : path_(std::move(path)) {}

    ScanCache::Fingerprint ScanCache::FingerprintFile(const std::filesystem::path& path) noexcept
    {
        try {
            std::error_code error;
            const auto size = std::filesystem::file_size(path, error);
            if (error) return {};
            const auto modified = std::filesystem::last_write_time(path, error);
            if (error) return {};
            return { .size = size, .modified = modified.time_since_epoch().count(),
                .sample = 0, .valid = true };
        } catch (...) {
            return {};
        }
    }

    std::string ScanCache::Key(const std::string_view scanner, const std::filesystem::path& source)
    {
        return std::string(scanner) + '\x1F' + Utf8(source.lexically_normal());
    }

    void ScanCache::Load() noexcept
    {
        std::scoped_lock lock(mutex_);
        if (loaded_) return;
        loaded_ = true;
        try {
            std::error_code error;
            const auto size = std::filesystem::file_size(path_, error);
            if (error || size > kMaximumCacheBytes) return;
            std::ifstream input(path_, std::ios::binary);
            std::array<char, kMagic.size()> magic{};
            std::uint32_t schema{}, count{};
            input.read(magic.data(), static_cast<std::streamsize>(magic.size()));
            if (!input || magic != kMagic || !Read(input, schema) || schema != kSchemaVersion ||
                !Read(input, count) || count > kMaximumEntries) return;
            std::unordered_map<std::string, Entry> loaded;
            loaded.reserve(count);
            for (std::uint32_t index = 0; index < count; ++index) {
                std::string key;
                Entry entry;
                std::uint8_t valid{};
                std::uint32_t recordCount{};
                if (!ReadString(input, key) || !Read(input, entry.fingerprint.size) ||
                    !Read(input, entry.fingerprint.modified) || !Read(input, entry.fingerprint.sample) ||
                    !Read(input, valid) || !Read(input, recordCount) || recordCount > kMaximumRecordsPerEntry) return;
                entry.fingerprint.valid = valid != 0;
                entry.records.resize(recordCount);
                for (auto& record : entry.records) if (!ReadRecord(input, record)) return;
                loaded.emplace(std::move(key), std::move(entry));
            }
            entries_ = std::move(loaded);
        } catch (...) {
            entries_.clear();
        }
    }

    std::optional<std::vector<HotkeyRecord>> ScanCache::Find(
        const std::string_view scanner, const std::filesystem::path& source) noexcept
    {
        try {
            const auto key = Key(scanner, source);
            Fingerprint expected;
            {
                std::scoped_lock lock(mutex_);
                touched_.insert(key);
                const auto found = entries_.find(key);
                if (found == entries_.end()) {
                    ++misses_;
                    return std::nullopt;
                }
                expected = found->second.fingerprint;
            }
            const auto fingerprint = FingerprintFile(source);
            if (!fingerprint.valid || expected != fingerprint) {
                std::scoped_lock lock(mutex_);
                ++misses_;
                return std::nullopt;
            }
            std::scoped_lock lock(mutex_);
            const auto found = entries_.find(key);
            if (found == entries_.end() || found->second.fingerprint != expected) {
                ++misses_;
                return std::nullopt;
            }
            ++hits_;
            return found->second.records;
        } catch (...) {
            return std::nullopt;
        }
    }

    void ScanCache::Store(const std::string_view scanner, const std::filesystem::path& source,
        const std::vector<HotkeyRecord>& records) noexcept
    {
        if (records.size() > kMaximumRecordsPerEntry) return;
        const auto fingerprint = FingerprintFile(source);
        if (!fingerprint.valid) return;
        try {
            const auto key = Key(scanner, source);
            std::scoped_lock lock(mutex_);
            touched_.insert(key);
            if (entries_.size() >= kMaximumEntries && !entries_.contains(key)) return;
            entries_[key] = { .fingerprint = fingerprint, .records = records };
        } catch (...) {
        }
    }

    void ScanCache::Save() noexcept
    {
        std::scoped_lock lock(mutex_);
        try {
            // Keep entries used by this virtual profile/run. This naturally
            // drops stale MO2 winners and bounds long-term cache growth.
            std::erase_if(entries_, [&](const auto& item) { return !touched_.contains(item.first); });
            std::error_code error;
            std::filesystem::create_directories(path_.parent_path(), error);
            if (error) return;
            auto temporary = path_;
            temporary += ".tmp";
            std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
            if (!output) return;
            output.write(kMagic.data(), static_cast<std::streamsize>(kMagic.size()));
            Write(output, kSchemaVersion);
            Write(output, static_cast<std::uint32_t>(entries_.size()));
            for (const auto& [key, entry] : entries_) {
                WriteString(output, key);
                Write(output, entry.fingerprint.size); Write(output, entry.fingerprint.modified);
                Write(output, entry.fingerprint.sample); Write(output, static_cast<std::uint8_t>(entry.fingerprint.valid));
                Write(output, static_cast<std::uint32_t>(entry.records.size()));
                for (const auto& record : entry.records) WriteRecord(output, record);
            }
            output.close();
            if (!output) { std::filesystem::remove(temporary, error); return; }
#ifdef _WIN32
            if (!MoveFileExW(temporary.c_str(), path_.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
                std::filesystem::remove(temporary, error);
            }
#else
            std::filesystem::rename(temporary, path_, error);
            if (error) std::filesystem::remove(temporary, error);
#endif
        } catch (...) {
        }
    }

    std::size_t ScanCache::HitCount() const noexcept { std::scoped_lock lock(mutex_); return hits_; }
    std::size_t ScanCache::MissCount() const noexcept { std::scoped_lock lock(mutex_); return misses_; }
}
