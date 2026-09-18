#pragma once

namespace UHI::NativeImGuiHost
{
    using RenderCallback = void (*)();
    using CloseCallback = void (*)();

    bool InstallRendererHook();
    bool Register(RenderCallback callback, CloseCallback closed);
    bool Open();
    bool Toggle();
    bool Close();
    void SubmitMouseWheel(float delta) noexcept;
    void SubmitModalMouseButton(unsigned button, bool down) noexcept;
    void SetAppearance(bool readableTheme) noexcept;
    [[nodiscard]] bool IsOpen() noexcept;
    [[nodiscard]] bool IsReady() noexcept;
}
