#pragma once

#include "UHI/HotkeyRecord.h"
#include "UHI/ScanControl.h"

#include <span>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace UHI
{
    enum class ConflictStatus : std::uint8_t
    {
        none,
        conditional,
        confirmed
    };

    struct ConflictGroup
    {
        std::vector<std::size_t> indices;
        ConflictStatus status{ ConflictStatus::none };
    };

    struct ConflictAnalysis
    {
        std::unordered_map<std::string, ConflictGroup> groups;
        std::vector<ConflictStatus> recordStatus;
        std::vector<std::vector<std::size_t>> confirmedPeers;
        std::vector<std::vector<std::size_t>> conditionalPeers;
    };

    class Registry
    {
    public:
        static constexpr std::size_t kDefaultMaxRecords = kMaximumPublishedRecords;

        explicit Registry(std::size_t maxRecords = kDefaultMaxRecords) noexcept;
        void Add(HotkeyRecord record);
        [[nodiscard]] std::span<const HotkeyRecord> Records() const noexcept;
        [[nodiscard]] std::unordered_map<std::string, std::vector<std::size_t>> Conflicts() const;
        [[nodiscard]] ConflictAnalysis AnalyzeConflicts() const;
        [[nodiscard]] std::size_t ConditionalConflictCount() const;
        [[nodiscard]] bool WasTruncated() const noexcept;
        [[nodiscard]] std::size_t DuplicateCount() const noexcept;
        [[nodiscard]] std::size_t DroppedCount() const noexcept;

    private:
        std::size_t maxRecords_;
        std::vector<HotkeyRecord> records_;
        std::unordered_set<std::string> identities_;
        std::size_t duplicateCount_{};
        std::size_t droppedCount_{};
    };
}
