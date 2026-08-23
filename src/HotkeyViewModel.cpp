#include "UHI/HotkeyViewModel.h"

#include <algorithm>
#include <iterator>
#include <unordered_set>

namespace UHI
{
    std::vector<HotkeyViewGroup> BuildHotkeyView(const Registry& registry)
    {
        const auto analysis = registry.AnalyzeConflicts();

        std::vector<HotkeyViewGroup> groups;
        std::unordered_set<std::string> displayed;
        std::size_t recordIndex{};
        for (const auto& record : registry.Records()) {
            const bool physicalDevice = record.device == "keyboard" || record.device == "mouse" ||
                record.device == "gamepad";
            // Scanner evidence with an unresolved code remains in reports and
            // snapshots, but it is not a physical binding and therefore does
            // not belong in the device map or the normal shortcut list.
            if (!record.runtimeActive || !record.conflictEligible || record.uiLocalOnly ||
                !physicalDevice || record.binding.empty()) {
                ++recordIndex;
                continue;
            }
            const auto displayIdentity = record.owner + '\x1F' + record.action + '\x1F' +
                record.device + '\x1F' + NormalizeBinding(record.binding);
            // Multiple files/scanners may corroborate the same effective
            // shortcut. Preserve every source in the raw report but render the
            // semantic binding once in the normal inventory.
            if (!displayed.insert(displayIdentity).second) {
                ++recordIndex;
                continue;
            }
            auto group = std::find_if(groups.begin(), groups.end(), [&](const HotkeyViewGroup& candidate) {
                return candidate.device == record.device;
            });
            if (group == groups.end()) {
                groups.push_back({ .device = record.device });
                group = std::prev(groups.end());
            }
            std::vector<const HotkeyRecord*> confirmedPeers;
            std::vector<const HotkeyRecord*> conditionalPeers;
            confirmedPeers.reserve(analysis.confirmedPeers[recordIndex].size());
            conditionalPeers.reserve(analysis.conditionalPeers[recordIndex].size());
            for (const auto peer : analysis.confirmedPeers[recordIndex]) {
                confirmedPeers.push_back(&registry.Records()[peer]);
            }
            for (const auto peer : analysis.conditionalPeers[recordIndex]) {
                conditionalPeers.push_back(&registry.Records()[peer]);
            }
            group->entries.push_back({
                .record = &record,
                .displayBinding = CompactBindingLabel(record.binding),
                .category = ClassifyHotkey(record),
                .conflict = analysis.recordStatus[recordIndex] == ConflictStatus::confirmed,
                .conditionalConflict = analysis.recordStatus[recordIndex] == ConflictStatus::conditional,
                .confirmedPeers = std::move(confirmedPeers),
                .conditionalPeers = std::move(conditionalPeers)
            });
            ++recordIndex;
        }

        return groups;
    }

    std::vector<HotkeyViewGroup> FilterHotkeyView(
        const std::vector<HotkeyViewGroup>& source,
        const HotkeyCategory category,
        const std::string_view device,
        const bool conflictsOnly)
    {
        std::vector<HotkeyViewGroup> filtered;
        for (const auto& group : source) {
            if (!device.empty() && group.device != device) {
                continue;
            }
            HotkeyViewGroup result{ .device = group.device };
            for (const auto& entry : group.entries) {
                if ((category != HotkeyCategory::all && entry.category != category) ||
                    (conflictsOnly && !entry.conflict)) {
                    continue;
                }
                result.entries.push_back(entry);
            }
            if (!result.entries.empty()) {
                filtered.push_back(std::move(result));
            }
        }
        return filtered;
    }
}
