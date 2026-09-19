#pragma once

#include "UHI/Registry.h"
#include "UHI/HotkeyCategory.h"

#include <string>
#include <memory>
#include <string_view>
#include <vector>

namespace UHI
{
    struct HotkeyViewEntry
    {
        const HotkeyRecord* record{};
        std::string displayBinding;
        HotkeyCategory category{ HotkeyCategory::user_interface };
        bool conflict{ false };
        bool conditionalConflict{ false };
        // The projection already borrows record pointers from this registry.
        const Registry* registry{};
        std::shared_ptr<const ConflictGroup> peerGroup;
        [[nodiscard]] std::vector<const HotkeyRecord*> Peers() const;
    };

    struct HotkeyViewGroup
    {
        std::string device;
        std::vector<HotkeyViewEntry> entries;
    };

    // Read-only projection used by the device and manager views. It keeps the full record for
    // tooltips while exposing compact labels and conflict state for rendering.
    std::vector<HotkeyViewGroup> BuildHotkeyView(const Registry& registry, bool includeUiLocal = false);
    std::vector<HotkeyViewGroup> BuildHotkeyView(const Registry& registry,
        ConflictAnalysis analysis, bool includeUiLocal);

    std::vector<HotkeyViewGroup> FilterHotkeyView(
        const std::vector<HotkeyViewGroup>& source,
        HotkeyCategory category,
        std::string_view device = {},
        bool conflictsOnly = false);
}
