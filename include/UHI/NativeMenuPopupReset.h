#pragma once

#include <imgui.h>
#include <imgui_internal.h>

namespace UHI
{
    // Called in the host's ImGui context after NewFrame, before drawing UHM.
    inline void ResetNativeMenuPopups()
    {
        // ClosePopupToLevel(0, ...) requires a nonempty stack and dereferences
        // entry zero even in release builds. A fresh context has no storage.
        // This upstream wrapper handles an empty stack and also closes modals.
        ImGui::ClosePopupsOverWindow(nullptr, false);
    }
}
