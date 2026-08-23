#include "UHI/OpeningHotkey.h"
#include "UHI/PathEncoding.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

namespace
{
    std::string ReadAll(const std::filesystem::path& path)
    {
        std::ifstream input(path, std::ios::binary);
        return { std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>() };
    }
}

int main()
{
    const std::string mixedUtf8 =
        "\xED\x95\x9C\xEA\xB8\x80/\xE4\xB8\xAD\xE6\x96\x87/English.ini";
    if (UHI::PathToUtf8(UHI::PathFromUtf8(mixedUtf8)) != mixedUtf8) return 1;

    const auto root = std::filesystem::temp_directory_path() / "uhi_opening_hotkey_test";
    const auto path = root / "UniversalHotkeyManager.ini";
    std::error_code error;
    std::filesystem::remove_all(root, error);

    const auto defaults = UHI::LoadOpeningHotkey(path);
    if (defaults.scanCode != 0xD3 || defaults.ctrl || defaults.shift || defaults.alt ||
        defaults.uiScale != 1.0F || defaults.uiLanguage != UHI::UiLanguage::automatic ||
        defaults.windowOpacity != 0.90F ||
        UHI::FormatOpeningHotkey(defaults) != "Del") return 1;

    const UHI::OpeningHotkey changed{ .scanCode = 0xD3, .shift = true,
        .shiftScanCode = 0x36, .uiScale = 1.20F, .windowOpacity = 0.72F,
        .uiLanguage = UHI::UiLanguage::korean };
    if (!UHI::SaveOpeningHotkey(path, changed)) return 1;
    const auto loaded = UHI::LoadOpeningHotkey(path);
    if (loaded != changed || UHI::FormatOpeningHotkey(loaded) != "RShift+Del") return 1;

    {
        std::ofstream output(path, std::ios::trunc);
        output << "[General]\nToggleKey=0x1D\nCtrl=true\n";
    }
    const auto invalid = UHI::LoadOpeningHotkey(path);

    const auto frameworkPath = root / "SKSEMenuFramework.ini";
    {
        std::ofstream output(frameworkPath, std::ios::binary | std::ios::trunc);
        output << "[Fonts]\r\nPrimaryFont = MainFont.ttf\r\n"
                  "EnableKorean = false ; preserve this comment\r\n"
                  "EnableChinese = false\r\n[Window]\r\nScale = 1.0\r\n";
    }
    if (UHI::EnsureMenuFrameworkGlyphRange(frameworkPath, UHI::UiLanguage::korean) !=
        UHI::MenuFrameworkGlyphRangeStatus::updated) return 1;
    const auto koreanEnabled = ReadAll(frameworkPath);
    if (!koreanEnabled.contains("EnableKorean = true ; preserve this comment\r\n") ||
        !koreanEnabled.contains("PrimaryFont = MainFont.ttf\r\n") ||
        !koreanEnabled.contains("[Window]\r\nScale = 1.0\r\n")) return 1;
    if (UHI::EnsureMenuFrameworkGlyphRange(frameworkPath, UHI::UiLanguage::korean) !=
        UHI::MenuFrameworkGlyphRangeStatus::alreadyEnabled) return 1;

    {
        std::ofstream output(frameworkPath, std::ios::binary | std::ios::trunc);
        output << "[Fonts]\nPrimaryFont=MainFont.ttf\n[Window]\nScale=1.0\n";
    }
    if (UHI::EnsureMenuFrameworkGlyphRange(frameworkPath, UHI::UiLanguage::chinese) !=
        UHI::MenuFrameworkGlyphRangeStatus::updated) return 1;
    const auto chineseEnabled = ReadAll(frameworkPath);
    const auto chinesePosition = chineseEnabled.find("EnableChinese = true");
    const auto windowPosition = chineseEnabled.find("[Window]");
    if (chinesePosition == std::string::npos || windowPosition == std::string::npos ||
        chinesePosition > windowPosition) return 1;

    const auto unchangedEnglish = ReadAll(frameworkPath);
    if (UHI::EnsureMenuFrameworkGlyphRange(frameworkPath, UHI::UiLanguage::english) !=
        UHI::MenuFrameworkGlyphRangeStatus::notRequired ||
        ReadAll(frameworkPath) != unchangedEnglish) return 1;
    if (UHI::EnsureMenuFrameworkGlyphRange(root / "missing.ini", UHI::UiLanguage::korean) !=
        UHI::MenuFrameworkGlyphRangeStatus::configMissing) return 1;

    std::filesystem::remove_all(root, error);
    if (invalid != UHI::OpeningHotkey{}) return 1;
    std::cout << "Opening hotkey settings test passed\n";
    return 0;
}
