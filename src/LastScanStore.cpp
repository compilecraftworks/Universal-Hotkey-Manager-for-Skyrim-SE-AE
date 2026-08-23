#include "UHI/LastScanStore.h"
#include "UHI/Registry.h"
#include "UHI/PathEncoding.h"

#include <array>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <string>
#include <system_error>
#ifdef _WIN32
#include <Windows.h>
#endif

namespace
{
    constexpr std::array<char, 8> kMagic{ 'U', 'H', 'M', 'L', 'A', 'S', 'T', '1' };
    constexpr std::uintmax_t kMaximumSnapshotBytes = 128U * 1024U * 1024U;
    constexpr std::size_t kMaximumStringBytes = 1U * 1024U * 1024U;

    struct Fingerprint
    {
        std::uint64_t size{};
        std::int64_t modified{};
        bool valid{};
        bool operator==(const Fingerprint&) const = default;
    };

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

    bool WriteString(std::ostream& output, const std::string_view value)
    {
        if (value.size() > kMaximumStringBytes) return false;
        Write(output, static_cast<std::uint32_t>(value.size()));
        output.write(value.data(), static_cast<std::streamsize>(value.size()));
        return static_cast<bool>(output);
    }

    std::filesystem::path PhysicalEvidencePath(const std::filesystem::path& evidence)
    {
        const auto text = UHI::PathToUtf8(evidence);
        const auto archiveSeparator = text.find("::");
        return archiveSeparator == std::string::npos ? evidence :
            UHI::PathFromUtf8(std::string_view(text).substr(0, archiveSeparator));
    }

    Fingerprint FingerprintFile(const std::filesystem::path& evidence) noexcept
    {
        try {
            const auto path = PhysicalEvidencePath(evidence);
            std::error_code error;
            if (!std::filesystem::is_regular_file(path, error) || error) return {};
            const auto size = std::filesystem::file_size(path, error);
            if (error) return {};
            const auto modified = std::filesystem::last_write_time(path, error);
            if (error) return {};
            return { .size = size, .modified = modified.time_since_epoch().count(), .valid = true };
        } catch (...) {
            return {};
        }
    }

    bool WriteRecord(std::ostream& output, const UHI::HotkeyRecord& value)
    {
        if (!WriteString(output, value.owner) || !WriteString(output, value.action) ||
            !WriteString(output, value.binding) || !WriteString(output, value.rawBinding) ||
            !WriteString(output, value.settingName) || !WriteString(output, value.settingSection) ||
            !WriteString(output, value.codeSystem) || !WriteString(output, value.device) ||
            !WriteString(output, value.detector) || !WriteString(output, value.categoryHint) ||
            !WriteString(output, UHI::PathToUtf8(value.evidencePath))) return false;
        Write(output, static_cast<std::uint8_t>(value.confidence));
        Write(output, static_cast<std::uint8_t>(value.stage));
        Write(output, static_cast<std::uint64_t>(value.evidenceLine));
        Write(output, static_cast<std::uint8_t>(value.editable));
        Write(output, static_cast<std::uint8_t>(value.runtimeActive));
        Write(output, static_cast<std::uint8_t>(value.conflictEligible));
        Write(output, static_cast<std::uint8_t>(value.uiLocalOnly));
        Write(output, value.contextMask);
        Write(output, static_cast<std::uint8_t>(value.contextConfidence));
        const auto fingerprint = FingerprintFile(value.evidencePath);
        Write(output, fingerprint.size);
        Write(output, fingerprint.modified);
        Write(output, static_cast<std::uint8_t>(fingerprint.valid));
        return static_cast<bool>(output);
    }

    bool ReadRecord(std::istream& input, UHI::HotkeyRecord& value, Fingerprint& fingerprint)
    {
        std::string path;
        if (!ReadString(input, value.owner) || !ReadString(input, value.action) ||
            !ReadString(input, value.binding) || !ReadString(input, value.rawBinding) ||
            !ReadString(input, value.settingName) || !ReadString(input, value.settingSection) ||
            !ReadString(input, value.codeSystem) || !ReadString(input, value.device) ||
            !ReadString(input, value.detector) || !ReadString(input, value.categoryHint) ||
            !ReadString(input, path)) return false;
        std::uint8_t confidence{}, stage{}, editable{}, runtimeActive{}, eligible{}, uiLocalOnly{}, contextConfidence{}, fingerprintValid{};
        std::uint32_t contextMask{};
        std::uint64_t line{};
        if (!Read(input, confidence) || !Read(input, stage) || !Read(input, line) ||
            !Read(input, editable) || !Read(input, runtimeActive) || !Read(input, eligible) ||
            !Read(input, uiLocalOnly) || !Read(input, contextMask) ||
            !Read(input, contextConfidence) || !Read(input, fingerprint.size) ||
            !Read(input, fingerprint.modified) || !Read(input, fingerprintValid) ||
            confidence > 2 || stage > 3 || editable > 1 || runtimeActive > 1 || eligible > 1 ||
            uiLocalOnly > 1 || contextConfidence > 2 ||
            fingerprintValid > 1) return false;
        value.confidence = static_cast<UHI::Confidence>(confidence);
        value.stage = static_cast<UHI::ScanStage>(stage);
        value.evidenceLine = static_cast<std::size_t>(line);
        value.editable = editable != 0;
        value.runtimeActive = runtimeActive != 0;
        value.conflictEligible = eligible != 0;
        value.uiLocalOnly = uiLocalOnly != 0;
        value.contextMask = contextMask;
        value.contextConfidence = static_cast<UHI::ContextConfidence>(contextConfidence);
        value.evidencePath = UHI::PathFromUtf8(path);
        fingerprint.valid = fingerprintValid != 0;
        return true;
    }
}

namespace UHI
{
    bool LastScanStore::Save(const std::filesystem::path& path,
        const std::span<const HotkeyRecord> records) const noexcept
    {
        if (records.size() > Registry::kDefaultMaxRecords) return false;
        try {
            std::error_code error;
            std::filesystem::create_directories(path.parent_path(), error);
            if (error) return false;
            auto temporary = path;
            temporary += ".tmp";
            std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
            if (!output) return false;
            output.write(kMagic.data(), static_cast<std::streamsize>(kMagic.size()));
            Write(output, kSchemaVersion);
            Write(output, static_cast<std::uint32_t>(records.size()));
            for (const auto& record : records) {
                if (!WriteRecord(output, record)) {
                    output.close();
                    std::filesystem::remove(temporary, error);
                    return false;
                }
            }
            output.close();
            if (!output) {
                std::filesystem::remove(temporary, error);
                return false;
            }
#ifdef _WIN32
            if (!MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
                std::filesystem::remove(temporary, error);
                return false;
            }
#else
            std::filesystem::rename(temporary, path, error);
            if (error) {
                std::filesystem::remove(temporary, error);
                return false;
            }
#endif
            return true;
        } catch (...) {
            return false;
        }
    }

    std::optional<std::vector<HotkeyRecord>> LastScanStore::Load(
        const std::filesystem::path& path, const bool validateFingerprints) const noexcept
    {
        try {
            std::error_code error;
            const auto fileSize = std::filesystem::file_size(path, error);
            if (error || fileSize > kMaximumSnapshotBytes) return std::nullopt;
            std::ifstream input(path, std::ios::binary);
            std::array<char, kMagic.size()> magic{};
            std::uint32_t schema{}, count{};
            input.read(magic.data(), static_cast<std::streamsize>(magic.size()));
            if (!input || magic != kMagic || !Read(input, schema) || schema != kSchemaVersion ||
                !Read(input, count) || count > Registry::kDefaultMaxRecords) return std::nullopt;
            std::vector<HotkeyRecord> records(count);
            for (auto& record : records) {
                Fingerprint expected;
                if (!ReadRecord(input, record, expected)) return std::nullopt;
                NormalizeRecordDisplayNames(record);
                if (validateFingerprints && expected.valid) {
                    const auto current = FingerprintFile(record.evidencePath);
                    if (!current.valid || current != expected) return std::nullopt;
                }
            }
            return records;
        } catch (...) {
            return std::nullopt;
        }
    }
}
