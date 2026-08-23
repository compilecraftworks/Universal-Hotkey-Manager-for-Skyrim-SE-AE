#pragma once

#include "UHI/HotkeyViewModel.h"

#include <functional>
#include <string_view>

namespace UHI
{
    struct HotkeyViewState
    {
        bool open{ false };
        HotkeyCategory category{ HotkeyCategory::interface };
        std::string_view device{};
        bool conflictsOnly{ false };
    };

    // Framework-neutral rendering contract. The SKSE Menu Framework adapter
    // supplies the actual ImGui calls; core code remains testable without D3D.
    class HotkeyViewRenderer
    {
    public:
        using TextFn = std::function<void(std::string_view)>;
        using EntryFn = std::function<void(const HotkeyViewEntry&)>;

        void SetState(HotkeyViewState state) noexcept { state_ = state; }
        [[nodiscard]] const HotkeyViewState& State() const noexcept { return state_; }

        void Render(const Registry& registry, const TextFn& text, const EntryFn& entry) const;

    private:
        HotkeyViewState state_{};
    };
}
