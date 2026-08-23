#pragma once

#include "UHI/Registry.h"
#include "UHI/HotkeyCategory.h"

#include <string>
#include <string_view>
#include <vector>

namespace UHI
{
    struct HotkeyViewEntry
    {
        const HotkeyRecord* record{};
        std::string displayBinding;
        HotkeyCategory category{ HotkeyCategory::interface };
        bool conflict{ false };
        bool conditionalConflict{ false };
        std::vector<const HotkeyRecord*> confirmedPeers;
        std::vector<const HotkeyRecord*> conditionalPeers;
    };

    struct HotkeyViewGroup
    {
        std::string device;
        std::vector<HotkeyViewEntry> entries;
    };

    // Read-only projection used by the device and manager views. It keeps the full record for
    // tooltips while exposing compact labels and conflict state for rendering.
    std::vector<HotkeyViewGroup> BuildHotkeyView(const Registry& registry);

    std::vector<HotkeyViewGroup> FilterHotkeyView(
        const std::vector<HotkeyViewGroup>& source,
        HotkeyCategory category,
        std::string_view device = {},
        bool conflictsOnly = false);
}
