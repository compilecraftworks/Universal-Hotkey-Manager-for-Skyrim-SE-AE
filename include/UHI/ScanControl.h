#pragma once

#include <functional>
#include <filesystem>
#include <algorithm>
#include <iterator>
#include <vector>

namespace UHI
{
    // One extra item is intentionally retained by scanners. Registry publishes
    // at most 100,000 records and uses the extra item to surface truncation in
    // the UI instead of silently exhausting memory on malformed/mod-generated
    // input.
    inline constexpr std::size_t kMaximumPublishedRecords = 100'000;
    inline constexpr std::size_t kMaximumCollectedRecords = kMaximumPublishedRecords + 1;

    using CancelCallback = std::function<bool()>;
    using PathCallback = std::function<void(const std::filesystem::path&)>;
    using ItemProgressCallback = std::function<void(
        const std::filesystem::path&, std::size_t completed, std::size_t total,
        float currentFilePercent)>;

    inline bool ScanCancelled(const CancelCallback& cancel) noexcept
    {
        if (!cancel) return false;
        try {
            return cancel();
        } catch (...) {
            // A broken callback must stop work, never escape into Skyrim.
            return true;
        }
    }

    inline void ReportScanPath(const PathCallback& callback, const std::filesystem::path& path) noexcept
    {
        if (!callback) return;
        try {
            callback(path);
        } catch (...) {
            // Progress display is optional and may never break the scan.
        }
    }

    inline void ReportItemProgress(const ItemProgressCallback& callback, const std::filesystem::path& path,
        const std::size_t completed, const std::size_t total, const float currentFilePercent) noexcept
    {
        if (!callback) return;
        try {
            callback(path, completed, total, std::clamp(currentFilePercent, 0.0F, 100.0F));
        } catch (...) {
        }
    }

    template <class T>
    bool AppendScanResults(std::vector<T>& destination, std::vector<T>& source,
        const std::size_t limit = kMaximumCollectedRecords)
    {
        if (destination.size() >= limit) return true;
        const auto count = std::min(source.size(), limit - destination.size());
        destination.insert(destination.end(), std::make_move_iterator(source.begin()),
            std::make_move_iterator(source.begin() + static_cast<std::ptrdiff_t>(count)));
        return count != source.size() || destination.size() >= limit;
    }
}
