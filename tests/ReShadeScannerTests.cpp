#include "UHI/Registry.h"
#include "UHI/JsonReporter.h"
#include "UHI/scanners/ReShadeScanner.h"

#include <filesystem>
#include <fstream>
#include <iostream>

int main()
{
    const auto path = std::filesystem::temp_directory_path() / "uhi_reshade_test.ini";
    {
        std::ofstream output(path);
        output << "[INPUT]\n";
        output << "KeyOverlay=36,0,0,0\n";
        output << "KeyEffects=35,0,0,0\n";
        output << "KeyScreenshot=0,0,0,0\nKeyFPS=1,1,0,0\nPresetShortcutKeys=121,0,1,0,122,0,0,1\n";
    }

    UHI::Scanners::ReShadeScanner scanner;
    const auto records = scanner.Scan(path);
    std::filesystem::remove(path);

    if (records.size() != 5 || records[0].owner != "ReShade" || records[0].action != "Open overlay" ||
        records[0].binding != "Home" || records[1].binding != "End" || records[2].binding != "Ctrl+LMB" ||
        records[2].device != "mouse" || records[3].binding != "Shift+F10" || records[4].binding != "Alt+F11") {
        std::cerr << "ReShade scanner test failed\n";
        return 1;
    }

    if (UHI::Scanners::FormatReShadeBinding("36,1,1,1") != "Ctrl+Shift+Alt+Home" ||
        !UHI::Scanners::FormatReShadeBinding("0,0,0,0").empty()) {
        std::cerr << "ReShade key formatting test failed\n";
        return 1;
    }

    if (UHI::CompactBindingLabel("Ctrl+Shift+Alt+Page Up") != "Ctrl+Shift+Alt+PgUp" ||
        UHI::CompactBindingLabel("Left Alt") != "LAlt" ||
        UHI::CompactBindingLabel("Num Lock") != "NumLk" ||
        UHI::CompactBindingLabel("Numpad 7") != "Num7" ||
        UHI::CompactBindingLabel("Numpad Decimal") != "Num.") {
        std::cerr << "Compact binding label test failed\n";
        return 1;
    }

    if (UHI::CompactActionLabel(
            "Code of key to use for parrying. See https://www.creationkit.com/Input_Script") != "Parrying" ||
        UHI::CompactActionLabel(
            "Press this key to enter ready state, then fire arrow within the window") != "Ready" ||
        UHI::CompactActionLabel("Menu toggle key for keyboard") != "Menu toggle" ||
        UHI::CompactActionLabel("Custom Hotkeyfor Mid Stance") != "Mid Stance" ||
        UHI::CompactActionLabel("Custom Hotkeyfor High Stance") != "High Stance") {
        std::cerr << "Compact action label test failed\n";
        return 1;
    }

    UHI::HotkeyRecord sexLabLabel{
        .owner = "SexLab", .action = "Key Up", .settingName = "iKeyUp",
        .evidencePath = "Data/SKSE/Plugins/SexLabData/Settings.yaml"
    };
    UHI::HotkeyRecord translatedLabel{
        .owner = "ahzconfigmenu", .action = "$mHUD_ActivationHotKey"
    };
    UHI::HotkeyRecord gameLabel{
        .owner = "Skyrim default controls", .action = "Activate"
    };
    UHI::NormalizeRecordDisplayNames(sexLabLabel);
    UHI::NormalizeRecordDisplayNames(translatedLabel);
    UHI::NormalizeRecordDisplayNames(gameLabel);
    if (sexLabLabel.owner != "SexLab P+" || sexLabLabel.action != "Scene Menu Up" ||
        translatedLabel.action != "Activation Hotkey" ||
        gameLabel.owner != "Skyrim Controls" ||
        UHI::CanonicalModDisplayName("DKAF") != "Dynamic Key Action Framework NG" ||
        UHI::CanonicalModDisplayName("TorchesCandlelightLanterns") !=
            "Torches Candlelight and Lanterns" ||
        UHI::CanonicalModDisplayName("keybinds") != "keybinds") {
        std::cerr << "Display-name normalization test failed\n";
        return 1;
    }

    UHI::Registry registry;
    registry.Add(records[0]);
    registry.Add({ .owner = "Test Mod", .action = "Test", .binding = " home " });
    if (!registry.Conflicts().empty() || registry.ConditionalConflictCount() != 1) {
        std::cerr << "Unknown-context overlap classification failed\n";
        return 1;
    }
    registry.Add({ .owner = "Native hint", .action = "Unresolved", .binding = "Home", .conflictEligible = false });
    if (!registry.Conflicts().empty() || registry.ConditionalConflictCount() != 1) {
        std::cerr << "Unresolved candidate conflict filtering failed\n";
        return 1;
    }

    const auto reportPath = std::filesystem::temp_directory_path() / "uhi_report_test.json";
    UHI::JsonReporter{}.Write(reportPath, registry.Records());
    std::ifstream report(reportPath);
    std::string reportText;
    std::string reportLine;
    while (std::getline(report, reportLine)) {
        reportText += reportLine;
        reportText += '\n';
    }
    report.close();
    std::filesystem::remove(reportPath);
    if (reportText.find("\"binding\": \"Home\"") == std::string::npos ||
        reportText.find("\"displayBinding\": \"Home\"") == std::string::npos ||
        reportText.find("\"category\": \"External tools\"") == std::string::npos ||
        reportText.find("\"codeSystem\":") == std::string::npos ||
        reportText.find("\"activationContext\":") == std::string::npos ||
        reportText.find("\"schemaVersion\": 6") == std::string::npos ||
        reportText.find("\"uiLocalOnly\": false") == std::string::npos) {
        std::cerr << "JSON reporter test failed\n";
        return 1;
    }

    std::cout << "All UHI tests passed\n";
    return 0;
}
