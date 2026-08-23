#pragma once

#include "UHI/HotkeyRecord.h"

#include <filesystem>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace UHI
{
    class ScanCache
    {
    public:
        // Bump whenever scanner interpretation changes so an older negative
        // cache cannot hide newly supported bindings (for example F2/F4/F8).
        static constexpr std::uint32_t kSchemaVersion = 7;

        explicit ScanCache(std::filesystem::path path);
        void Load() noexcept;
        [[nodiscard]] std::optional<std::vector<HotkeyRecord>> Find(
            std::string_view scanner, const std::filesystem::path& source) noexcept;
        void Store(std::string_view scanner, const std::filesystem::path& source,
            const std::vector<HotkeyRecord>& records) noexcept;
        void Save() noexcept;

        [[nodiscard]] std::size_t HitCount() const noexcept;
        [[nodiscard]] std::size_t MissCount() const noexcept;

    private:
        struct Fingerprint
        {
            std::uint64_t size{};
            std::int64_t modified{};
            std::uint64_t sample{};
            bool valid{};

            bool operator==(const Fingerprint&) const = default;
        };

        struct Entry
        {
            Fingerprint fingerprint;
            std::vector<HotkeyRecord> records;
        };

        [[nodiscard]] static Fingerprint FingerprintFile(const std::filesystem::path& path) noexcept;
        [[nodiscard]] static std::string Key(std::string_view scanner, const std::filesystem::path& source);

        std::filesystem::path path_;
        mutable std::mutex mutex_;
        std::unordered_map<std::string, Entry> entries_;
        std::unordered_set<std::string> touched_;
        std::size_t hits_{};
        std::size_t misses_{};
        bool loaded_{};
    };
}
