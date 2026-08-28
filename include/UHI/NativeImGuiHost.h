#pragma once

namespace UHI::NativeImGuiHost
{
    using RenderCallback = void (*)();

    bool Register(RenderCallback callback);
    bool Toggle();
    bool Close();
    [[nodiscard]] bool IsOpen() noexcept;
}
