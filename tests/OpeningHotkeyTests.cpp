#include "UHI/OpeningHotkey.h"
#include "UHI/PathEncoding.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

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

    std::filesystem::remove_all(root, error);
    if (invalid != UHI::OpeningHotkey{}) return 1;
    std::cout << "Opening hotkey settings test passed\n";
    return 0;
}
