#pragma once

namespace UHI::NativeImGuiHost
{
    using RenderCallback = void (*)();

    bool InstallRendererHook();
    bool Register(RenderCallback callback);
    bool Open();
    bool Toggle();
    bool Close();
    void SubmitMouseWheel(float delta) noexcept;
    [[nodiscard]] bool IsOpen() noexcept;
    [[nodiscard]] bool IsReady() noexcept;
}
