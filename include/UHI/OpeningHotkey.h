#pragma once

#include <cstdint>
#include <compare>
#include <filesystem>
#include <string>

namespace UHI
{
    enum class UiLanguage : std::uint8_t
    {
        automatic,
        english,
        korean,
        chinese
    };

    enum class MenuFrameworkGlyphRangeStatus : std::uint8_t
    {
        notRequired,
        alreadyEnabled,
        updated,
        configMissing,
        writeFailed
    };

    struct OpeningHotkey
    {
        std::uint32_t scanCode{ 0xD3 }; // Delete
        bool ctrl{};
        bool shift{};
        bool alt{};
        // Zero accepts either side for backwards-compatible INI files. A
        // captured shortcut stores the exact left/right DirectInput code.
        std::uint32_t ctrlScanCode{};
        std::uint32_t shiftScanCode{};
        std::uint32_t altScanCode{};
        float uiScale{ 1.0F };
        // Main manager background opacity only. Text, controls, device
        // diagrams, and modal popups remain fully opaque for readability.
        float windowOpacity{ 0.90F };
        UiLanguage uiLanguage{ UiLanguage::automatic };

        auto operator<=>(const OpeningHotkey&) const = default;
    };

    [[nodiscard]] bool IsValidOpeningHotkey(const OpeningHotkey& hotkey) noexcept;
    [[nodiscard]] std::string FormatOpeningHotkey(const OpeningHotkey& hotkey);
    // Resolve Automatic using Windows' display-language and user-locale
    // settings. This does not depend on the optional system-wide UTF-8 locale.
    [[nodiscard]] UiLanguage ResolveUiLanguage(UiLanguage configured) noexcept;
    // Preserve every unrelated Menu Framework setting while enabling the
    // glyph range required by the effective UHM language. The framework's
    // configured PrimaryFont remains untouched.
    [[nodiscard]] MenuFrameworkGlyphRangeStatus EnsureMenuFrameworkGlyphRange(
        const std::filesystem::path& path, UiLanguage language) noexcept;
    [[nodiscard]] OpeningHotkey LoadOpeningHotkey(const std::filesystem::path& path) noexcept;
    [[nodiscard]] bool SaveOpeningHotkey(const std::filesystem::path& path,
        const OpeningHotkey& hotkey) noexcept;
}
