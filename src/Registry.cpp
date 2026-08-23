#include "UHI/Registry.h"
#include "UHI/PathEncoding.h"

#include <cstdint>

namespace UHI
{
    namespace
    {
        constexpr std::uint32_t Context(UHI::ActivationContext value) noexcept
        {
            return static_cast<std::uint32_t>(value);
        }

        std::uint32_t ExpandedContextDomain(const std::uint32_t context) noexcept
        {
            constexpr std::uint32_t menuSpecific =
                Context(UHI::ActivationContext::console) |
                Context(UHI::ActivationContext::itemMenu) |
                Context(UHI::ActivationContext::inventory) |
                Context(UHI::ActivationContext::favorites) |
                Context(UHI::ActivationContext::map) |
                Context(UHI::ActivationContext::stats) |
                Context(UHI::ActivationContext::cursor) |
                Context(UHI::ActivationContext::book) |
                Context(UHI::ActivationContext::journal) |
                Context(UHI::ActivationContext::lockpicking) |
                Context(UHI::ActivationContext::debugText) |
                Context(UHI::ActivationContext::debugOverlay) |
                Context(UHI::ActivationContext::mapDebug) |
                Context(UHI::ActivationContext::marketplace) |
                Context(UHI::ActivationContext::favor);
            constexpr std::uint32_t allConcrete =
                Context(UHI::ActivationContext::gameplay) | menuSpecific |
                Context(UHI::ActivationContext::menuMode) |
                Context(UHI::ActivationContext::externalOverlay) |
                Context(UHI::ActivationContext::tfcMode) |
                Context(UHI::ActivationContext::sexLabScene);
            if ((context & Context(UHI::ActivationContext::global)) != 0) return allConcrete;

            auto expanded = context & allConcrete;
            // Engine menu-mode and item-menu mappings are common layers that
            // coexist with their specialized ControlMap contexts.
            if ((context & Context(UHI::ActivationContext::menuMode)) != 0) expanded |= menuSpecific;
            if ((context & Context(UHI::ActivationContext::itemMenu)) != 0) {
                expanded |= Context(UHI::ActivationContext::inventory);
            }
            // Free-camera operation is a nested gameplay state. SexLab scene
            // controls are intentionally kept separate: the framework and
            // Utility Plus gate those handlers on an active animation event,
            // so ordinary field bindings on the same physical key are safe.
            if ((context & Context(UHI::ActivationContext::gameplay)) != 0) {
                expanded |= Context(UHI::ActivationContext::tfcMode);
            }
            if ((context & Context(UHI::ActivationContext::tfcMode)) != 0) {
                expanded |= Context(UHI::ActivationContext::gameplay);
            }
            // An external overlay receives input while an underlying Skyrim
            // context is active, so it intersects every concrete state.
            if ((context & Context(UHI::ActivationContext::externalOverlay)) != 0) return allConcrete;
            return expanded;
        }

        bool ContextsOverlap(const std::uint32_t left, const std::uint32_t right) noexcept
        {
            // Conflict is a set-intersection question: if no possible active
            // engine state satisfies both constraints, the reused key is safe.
            return (ExpandedContextDomain(left) & ExpandedContextDomain(right)) != 0;
        }

        UHI::ConflictStatus PairStatus(const UHI::HotkeyRecord& left,
            const UHI::HotkeyRecord& right) noexcept
        {
            // controlmap.txt intentionally reuses physical inputs across its
            // engine contexts. Those entries are one coherent game mapping,
            // not conflicts with other entries from the same control map.
            if (left.detector == "ControlMapScanner" && right.detector == "ControlMapScanner" &&
                left.owner == right.owner) return UHI::ConflictStatus::none;
            if (left.contextMask == 0 || right.contextMask == 0) {
                // A missing context is never enough evidence for a confirmed
                // conflict. Unknown+unknown and confirmed+unknown bindings are
                // both retained as conditional overlaps until both activation
                // domains are proven.
                return UHI::ConflictStatus::conditional;
            }
            if (!ContextsOverlap(left.contextMask, right.contextMask)) {
                return UHI::ConflictStatus::none;
            }
            if (left.contextConfidence == UHI::ContextConfidence::confirmed &&
                right.contextConfidence == UHI::ContextConfidence::confirmed) {
                return UHI::ConflictStatus::confirmed;
            }
            return UHI::ConflictStatus::conditional;
        }

        std::string ConflictKey(const HotkeyRecord& record)
        {
            return record.device + "\x1F" + NormalizeBinding(record.binding);
        }

        std::string RecordIdentity(const HotkeyRecord& record, const std::string& normalizedBinding)
        {
            constexpr char separator = '\x1F';
            std::string identity;
            identity.reserve(record.owner.size() + record.action.size() + record.device.size() +
                record.detector.size() + record.evidencePath.native().size() + normalizedBinding.size() + 64);
            identity.append(record.owner).push_back(separator);
            identity.append(record.action).push_back(separator);
            identity.append(normalizedBinding).push_back(separator);
            identity.append(record.device).push_back(separator);
            identity.append(record.detector).push_back(separator);
            try {
                identity.append(UHI::PathToUtf8(record.evidencePath.lexically_normal()));
            } catch (...) {
                identity.append("<unrepresentable-path>");
            }
            identity.push_back(separator);
            identity.append(std::to_string(record.evidenceLine)).push_back(separator);
            identity.append(std::to_string(static_cast<std::uint32_t>(record.stage)));
            return identity;
        }
    }

    Registry::Registry(const std::size_t maxRecords) noexcept :
        maxRecords_(maxRecords)
    {}

    void Registry::Add(HotkeyRecord record)
    {
        // Sanitize restored scans as well as newly scanned records. Older
        // caches could contain static CommonLib candidates marked as active,
        // which made implementation details look like real hotkeys.
        if (record.detector == "StaticCommonLibInputHandler") {
            record.runtimeActive = false;
            record.editable = false;
            record.conflictEligible = false;
        }
        constexpr auto hiddenGameContexts = Context(ActivationContext::console) |
            Context(ActivationContext::debugText) |
            Context(ActivationContext::debugOverlay) | Context(ActivationContext::mapDebug) |
            Context(ActivationContext::marketplace);
        if (record.detector == "ControlMapScanner" && (record.contextMask & hiddenGameContexts) != 0) {
            return;
        }
        const auto normalizedBinding = NormalizeBinding(record.binding);
        if (normalizedBinding.empty()) {
            return;
        }
        if (records_.size() >= maxRecords_) {
            ++droppedCount_;
            return;
        }
        if (!identities_.insert(RecordIdentity(record, normalizedBinding)).second) {
            ++duplicateCount_;
            return;
        }
        if (record.contextMask == 0 && record.detector == "ControlMapScanner") {
            record.contextMask = Context(ActivationContext::gameplay);
            record.contextConfidence = ContextConfidence::confirmed;
        } else if (record.contextMask == 0 &&
            (record.detector == "ReShadeScanner" || record.detector == "EnbScanner" ||
                record.detector == "CommunityShadersScanner")) {
            record.contextMask = Context(ActivationContext::global);
            record.contextConfidence = ContextConfidence::inferred;
        }
        records_.push_back(std::move(record));
    }

    std::span<const HotkeyRecord> Registry::Records() const noexcept
    {
        return records_;
    }

    std::unordered_map<std::string, std::vector<std::size_t>> Registry::Conflicts() const
    {
        std::unordered_map<std::string, std::vector<std::size_t>> confirmed;
        for (const auto& [key, group] : AnalyzeConflicts().groups) {
            if (group.status == ConflictStatus::confirmed) confirmed.emplace(key, group.indices);
        }
        return confirmed;
    }

    ConflictAnalysis Registry::AnalyzeConflicts() const
    {
        ConflictAnalysis analysis;
        analysis.recordStatus.resize(records_.size(), ConflictStatus::none);
        analysis.confirmedPeers.resize(records_.size());
        analysis.conditionalPeers.resize(records_.size());
        std::unordered_map<std::string, std::vector<std::size_t>> grouped;
        for (std::size_t index = 0; index < records_.size(); ++index) {
            if (!records_[index].runtimeActive || !records_[index].conflictEligible || records_[index].uiLocalOnly) {
                continue;
            }
            grouped[ConflictKey(records_[index])].push_back(index);
        }
        std::erase_if(grouped, [&](const auto& item) {
            std::unordered_set<std::string> semanticActions;
            for (const auto index : item.second) {
                // Multiple scanners may independently prove the same binding.
                // That is corroborating evidence, not a hotkey conflict.
                semanticActions.insert(records_[index].owner + '\x1F' + records_[index].action);
            }
            return semanticActions.size() < 2;
        });
        for (auto& [key, indices] : grouped) {
            ConflictStatus groupStatus = ConflictStatus::none;
            for (std::size_t leftIndex = 0; leftIndex < indices.size(); ++leftIndex) {
                for (std::size_t rightIndex = leftIndex + 1; rightIndex < indices.size(); ++rightIndex) {
                    const auto left = indices[leftIndex];
                    const auto right = indices[rightIndex];
                    if (records_[left].owner == records_[right].owner &&
                        records_[left].action == records_[right].action) continue;
                    const auto status = PairStatus(records_[left], records_[right]);
                    if (status == ConflictStatus::confirmed) {
                        analysis.confirmedPeers[left].push_back(right);
                        analysis.confirmedPeers[right].push_back(left);
                    } else if (status == ConflictStatus::conditional) {
                        analysis.conditionalPeers[left].push_back(right);
                        analysis.conditionalPeers[right].push_back(left);
                    }
                    if (static_cast<unsigned>(status) > static_cast<unsigned>(analysis.recordStatus[left])) {
                        analysis.recordStatus[left] = status;
                    }
                    if (static_cast<unsigned>(status) > static_cast<unsigned>(analysis.recordStatus[right])) {
                        analysis.recordStatus[right] = status;
                    }
                    if (static_cast<unsigned>(status) > static_cast<unsigned>(groupStatus)) groupStatus = status;
                }
            }
            if (groupStatus != ConflictStatus::none) {
                analysis.groups.emplace(std::move(key), ConflictGroup{ std::move(indices), groupStatus });
            }
        }
        return analysis;
    }

    std::size_t Registry::ConditionalConflictCount() const
    {
        std::size_t count{};
        for (const auto& [key, group] : AnalyzeConflicts().groups) {
            (void)key;
            count += group.status == ConflictStatus::conditional ? 1U : 0U;
        }
        return count;
    }

    bool Registry::WasTruncated() const noexcept
    {
        return droppedCount_ != 0;
    }

    std::size_t Registry::DuplicateCount() const noexcept
    {
        return duplicateCount_;
    }

    std::size_t Registry::DroppedCount() const noexcept
    {
        return droppedCount_;
    }
}
