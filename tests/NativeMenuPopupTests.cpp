#include "UHI/NativeMenuPopupReset.h"

#include <iostream>

namespace
{
    int failures{};
    void Check(bool condition, const char* label)
    {
        if (!condition) { std::cerr << label << '\n'; ++failures; }
    }

    void BeginReopenedFrame()
    {
        auto& io = ImGui::GetIO();
        io.ClearEventsQueue();
        io.ClearInputKeys();
        io.ClearInputMouse();
        ImGui::NewFrame();
        UHI::ResetNativeMenuPopups();
        Check(ImGui::GetCurrentContext()->OpenPopupStack.empty(), "Reopen must clear all stale popups");
        Check(ImGui::GetCurrentContext()->BeginPopupStack.empty(), "Reset must precede popup rendering");
    }
}

int main()
{
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    auto& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.DisplaySize = ImVec2(1280, 720);
    unsigned char* pixels{};
    int width{}, height{};
    io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);

    Check(ImGui::GetCurrentContext()->OpenPopupStack.Data == nullptr, "Fresh context has no popup storage");
    BeginReopenedFrame(); // 1.0.8 crashed here with the release ImGui library.
    UHI::ResetNativeMenuPopups(); // Repeated cleanup must also be safe.
    ImGui::Render();

    for (int cycle = 0; cycle < 100; ++cycle) {
        BeginReopenedFrame();
        ImGui::Begin("UHM test window");
        ImGui::OpenPopup("Binding editor");
        const bool modal = ImGui::BeginPopupModal("Binding editor");
        Check(modal, "Create a real modal before simulated host closure");
        if (modal) {
            ImGui::OpenPopup("Nested selector");
            const bool nested = ImGui::BeginPopup("Nested selector");
            Check(nested, "Create nested popup");
            if (nested) ImGui::EndPopup();
            ImGui::EndPopup();
        }
        ImGui::End();
        ImGui::Render();
        Check(ImGui::GetCurrentContext()->OpenPopupStack.Size == 2, "Closure leaves both popups pending");

        // Host hide stops rendering; the next show takes the reset path.
        io.AddKeyEvent(ImGuiKey_A, true);
        io.AddMouseButtonEvent(0, true);
        io.AddMouseWheelEvent(0, 1);
        BeginReopenedFrame();
        Check(!ImGui::IsKeyDown(ImGuiKey_A) && !ImGui::IsMouseDown(0) && io.MouseWheel == 0,
            "Stale input must not survive reopening");
        ImGui::Begin("UHM test window");
        Check(!ImGui::IsPopupOpen("Binding editor"), "Modal stays closed after reset");
        // Cover a popup queued but not yet submitted to BeginPopup.
        ImGui::OpenPopup("Unsubmitted popup");
        ImGui::End();
        ImGui::Render();
        BeginReopenedFrame();
        ImGui::Render();
    }

    ImGui::DestroyContext();
    std::cout << "Native popup reset: first open, empty stack, modal/nested/unsubmitted popups, "
                 "100 reopen cycles; failures=" << failures << '\n';
    return failures == 0 ? 0 : 1;
}
