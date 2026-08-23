#include "UHI/HotkeyViewRenderer.h"

namespace UHI
{
    void HotkeyViewRenderer::Render(const Registry& registry, const TextFn& text, const EntryFn& entry) const
    {
        if (!state_.open || !text || !entry) {
            return;
        }

        const auto groups = FilterHotkeyView(
            BuildHotkeyView(registry), state_.category, state_.device, state_.conflictsOnly);
        text(HotkeyCategoryName(state_.category));
        for (const auto& group : groups) {
            text(group.device);
            for (const auto& item : group.entries) {
                entry(item);
            }
        }
    }
}
