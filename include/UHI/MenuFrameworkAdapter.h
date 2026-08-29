#pragma once

#include "UHI/Registry.h"
#include "UHI/OpeningHotkey.h"
#include <string_view>
#include <functional>
#include <memory>

namespace UHI
{
    bool RegisterMenuFrameworkWindow();
    void SetMenuFrameworkRegistry(std::shared_ptr<const Registry> registry, bool restored = false);
    void SetMenuFrameworkSexLabInstalled(bool installed) noexcept;
    bool OpenMenuFrameworkWindow();
    bool ToggleMenuFrameworkWindow();
    bool CloseMenuFrameworkWindow();
    bool IsMenuFrameworkWindowOpen() noexcept;
    bool BeginMenuFrameworkOpeningHotkeyCapture() noexcept;
    bool IsMenuFrameworkOpeningHotkeyCaptureActive() noexcept;
    bool CaptureMenuFrameworkOpeningHotkey(std::uint32_t scanCode,
        std::uint32_t ctrlScanCode, std::uint32_t shiftScanCode,
        std::uint32_t altScanCode) noexcept;
    void CancelMenuFrameworkOpeningHotkeyCapture() noexcept;
    // Rename and binding editors share one modal input lock. While active the
    // UHM input sink consumes game input so neither Skyrim nor another mod can
    // react to keys intended for the popup.
    bool IsMenuFrameworkModalInputActive() noexcept;
    // A popup/capture Escape must never be reused to close the parent UHM
    // window. The input poller clears this latch only after Escape is released.
    bool IsMenuFrameworkEscapeCloseSuppressed() noexcept;
    void ReleaseMenuFrameworkEscapeCloseSuppression() noexcept;
    bool IsMenuFrameworkBindingCaptureActive() noexcept;
    bool CaptureMenuFrameworkBindingInput(std::string_view device,
        std::uint32_t mainCode, std::string_view modifierDevice,
        std::uint32_t modifierCode) noexcept;
    void CancelMenuFrameworkBindingCapture() noexcept;
    void CancelMenuFrameworkEditorModal() noexcept;
    void SetMenuFrameworkScanStatus(bool running, float percent, float currentFilePercent,
        std::string_view stage, std::string_view path);
    void SetMenuFrameworkChangedHotkeyNotice(std::size_t count) noexcept;
    void SetMenuFrameworkStartScan(std::function<void()> startScan);
    void SetMenuFrameworkAutomaticRefresh(std::function<void()> refresh);
    void SetMenuFrameworkCancelScan(std::function<void()> cancelScan);
    void SetMenuFrameworkOpeningHotkey(
        std::function<OpeningHotkey()> getHotkey,
        std::function<bool(const OpeningHotkey&)> saveHotkey);
    // All binding writes are routed through the SKSE plugin so VM-backed MCM
    // values are changed on the game thread while document/controlmap values
    // keep using their exact source serializer.  Completion may be invoked
    // asynchronously and must not call ImGui directly.
    using BindingWriteCompletion = std::function<void(bool, std::string)>;
    using BindingWriter = std::function<void(HotkeyRecord, std::string, BindingWriteCompletion)>;
    void SetMenuFrameworkBindingWriter(BindingWriter writer);
}
