#include "UHI/BindingHistory.h"
#include "UHI/BindingInputCapture.h"
#include "UHI/BindingSerializer.h"
#include "UHI/ConfigBindingScope.h"
#include "UHI/HotkeyViewModel.h"
#include "UHI/McmBindingMatch.h"
#include "UHI/OpeningHotkey.h"
#include "UHI/ScanPipeline.h"
#include "UHI/SkyrimRuntimeLayout.h"
#include "UHI/scanners/ControlMapScanner.h"
#include "UHI/scanners/GenericConfigScanner.h"
#include "UHI/writers/ConfigFileWriter.h"
#include "UHI/writers/ControlMapWriter.h"
#include <filesystem>
#include <fstream>
#include <iostream>

namespace {
    int failures{};
    void Check(bool success, const char* label) { if (!success) { std::cerr << label << '\n'; ++failures; } }
}
int main()
{
    UHI::BindingInputCapture capture;
    capture.ObserveKeyboard(0x9D, true);
    Check(!capture.Process("keyboard", 0x9D, true, false), "Modifier must wait for the main input");
    Check(!capture.Process("keyboard", 0x9D, false, false), "Held event must not terminate capture");
    auto chord = capture.Process("keyboard", 0x3F, true, false);
    Check(chord && chord->code == 0x3F && chord->modifier == 0x9D, "Same-batch RCtrl+F5 capture");
    chord = capture.Process("mouse", 8, true, false);
    Check(chord && chord->modifierDevice == "keyboard" && chord->modifier == 0x9D, "Ctrl+wheel capture");
    capture.ObserveKeyboard(0x2A, true);
    chord = capture.Process("keyboard", 0x3F, true, false);
    Check(chord && chord->multipleModifiers, "Multiple modifiers must never silently lose a key");
    capture.ObserveKeyboard(0x9D, false);
    capture.ObserveKeyboard(0x2A, false);
    Check(!capture.Process("gamepad", 274, true, false), "Gamepad shoulder waits for chord");
    chord = capture.Process("gamepad", 276, true, false);
    Check(chord && chord->modifier == 274 && chord->code == 276, "Gamepad shoulder+A capture");
    Check(!capture.Process("mouse", 3, true, false), "Side button waits for chord");
    chord = capture.Process("mouse", 3, false, true);
    Check(chord && chord->code == 3 && chord->modifier == 0, "Standalone side button on release");

    UHI::HotkeyRecord record{ .owner="Example", .action="Speak", .binding="F5", .rawBinding="63",
        .settingName="SpeakKey", .codeSystem="SKSE unified input code", .detector="StructuredConfigScanner",
        .evidenceLine=2, .editable=true };
    Check(UHI::SerializeUnboundBinding(record).raw == "-1", "SKSE unbind sentinel");
    Check(!UHI::SerializeCapturedBinding(record, "keyboard", 63, "keyboard", 0x1D), "Scalar SKSE cannot store a chord");
    auto vkRecord = record; vkRecord.codeSystem = "Windows virtual-key code";
    Check(!UHI::SerializeCapturedBinding(vkRecord, "keyboard", 63, "keyboard", 0x1D), "Scalar VK cannot store a chord");
    Check(!UHI::SerializeUnboundBinding(vkRecord), "Unknown VK unbind convention must fail closed");
    Check(UHI::MatchesOwnerPrefixedSetting("KeyUp", "ExampleKeyUp", "Example Menu"), "Owner-prefixed custom getter");
    Check(!UHI::MatchesOwnerPrefixedSetting("KeyUp", "OtherKeyUp", "Example Menu"), "Do not join another mod's global");

    UHI::Scanners::GenericConfigScanner scanner;
    auto yaml = scanner.ScanContent("Data/SKSE/Plugins/Example/Hotkey.yaml",
        "# Windows virtual-key codes\nrecordSpeech: 115\ntextInput: 112\nduration: 20\n");
    Check(yaml.size() == 2 && yaml[0].binding == "F4" && yaml[1].binding == "F1", "Dedicated action-key YAML");
    auto nested = scanner.ScanContent("Data/Example/settings.json",
        R"({"hotkeys":{"speak":63,"nested":{"listen":64},"duration":20},"graphics":{"quality":65}})",
        {}, UHI::NumericCodeSpace::skseUnifiedInputCode);
    Check(nested.size() == 2 && nested[0].binding == "F5" && nested[1].binding == "F6", "JSON key-map scopes and sibling exclusion");
    auto nestedYaml = scanner.ScanContent("Data/Example/settings.yaml",
        "hotkeys:\n  speak: 63\n  listen: 64\ngraphics:\n  quality: 65\n", {}, UHI::NumericCodeSpace::skseUnifiedInputCode);
    Check(nestedYaml.size() == 2, "YAML indentation scopes and sibling exclusion");
    auto floats = scanner.ScanContent("Data/Example/MCMSettings.json",
        R"({"SetKeyUp":78.0,"SetKeyDown":74.000,"BadKey":74.5})", {}, UHI::NumericCodeSpace::skseUnifiedInputCode);
    Check(floats.size() == 2 && floats[0].binding == "Num+" && floats[1].binding == "Num-", "Integral native MCM float exports");
    auto unrelated = scanner.ScanContent("Data/Example/settings.yaml", "quality: 63\nvolume: 65\n");
    Check(unrelated.empty(), "Ordinary numeric settings stay excluded");

    std::vector<UHI::HotkeyRecord> modules{record};
    modules[0].detector = "CommunityShadersScanner";
    UHI::ApplyLoadedPluginState(modules, [](auto) { return false; });
    Check(!modules[0].runtimeActive, "Disabled DLL with leftover config is inactive");
    UHI::ApplyLoadedPluginState(modules, [](auto) { return true; });
    Check(modules[0].runtimeActive, "Restored snapshot uses newly loaded DLL state");
    UHI::Registry registry;
    record.uiLocalOnly = true;
    registry.Add(record);
    Check(UHI::BuildHotkeyView(registry).empty(), "Default core projection preserves UI-local filtering");
    const auto all = UHI::BuildHotkeyView(registry, true);
    Check(all.size() == 1 && all[0].entries.size() == 1 && !all[0].entries[0].conflict, "UI-local keys available without gameplay conflicts");
    record.uiLocalOnly = false;

    const auto root = std::filesystem::temp_directory_path() / "uhi_forum_regression";
    std::filesystem::create_directories(root);
    record.evidencePath = root / "settings.json";
    { std::ofstream out(record.evidencePath); out << "{\n\"SpeakKey\":63,\"Volume\":10\n}\n"; }
    const auto historyPath = root / "binding-history-v1.bin";
    UHI::Writers::ConfigFileWriter writer;
    Check(writer.SetBinding(record.evidencePath, 2, record.settingName, "63", "-1"), "Unbind source value");
    Check(UHI::SaveBindingChange(historyPath, record, "-1"), "Persist undo after unbinding");
    const auto original = UHI::OriginalBackupValue(record);
    Check(original && *original == "63", "Read original scalar from JSON backup");
    Check(writer.SetBinding(record.evidencePath, 2, "Volume", "10", "20"), "Independent setting changed");
    const auto history = UHI::LoadBindingHistory(historyPath);
    Check(history.size() == 1 && history[0].before.rawBinding == "63" && history[0].after.rawBinding == "-1", "History survives source changes and restart");
    if (original) Check(writer.SetBinding(record.evidencePath, 2, record.settingName, "-1", *original), "Restore only one setting");
    Check(writer.ReadBinding(record.evidencePath, 2, "Volume") == "20", "Restore preserves other values");
    Check(!writer.SetBinding(record.evidencePath, 2, record.settingName, "-1", "64"), "Stale history does not overwrite a changed binding");
    const auto control = root / "controlmap.txt";
    { std::ofstream out(control); out << "Jump 0x39 0xff 0xff 1 1 1\n"; }
    UHI::Writers::ControlMapWriter controlWriter;
    Check(!controlWriter.SetBinding(control, 1, "keyboard", "63", "Sprint", "0x39"), "Control-map action guard");
    Check(controlWriter.SetBinding(control, 1, "keyboard", "63", "Jump", "0x39"), "Control-map checked write");
    Check(!controlWriter.SetBinding(control, 1, "keyboard", "0xff", "Jump", "0x39"), "Control-map stale-value guard");
    const auto console = UHI::Scanners::ControlMapScanner{}.ScanContent(control,
        "Console 0x29 0xff 0xff 0 0 0\n// Console\nConsoleHistory 0x48 0xff 0xff 0 0 0\n");
    Check(console.size() == 1 && console[0].action == "Console" && !console[0].editable, "Console launcher is visible and read-only");
    auto preferences = UHI::OpeningHotkey{};
    preferences.enabled = false; preferences.readableTheme = true;
    Check(UHI::SaveOpeningHotkey(root / "UHM.ini", preferences) && UHI::LoadOpeningHotkey(root / "UHM.ini") == preferences,
        "Shortcut disable and appearance preferences persist");
    Check(UHI::SkyrimInputContextLayoutForVersion(1,5,97)->runtimeContextCount == 17 &&
        UHI::SkyrimInputContextLayoutForVersion(1,6,1130)->runtimeContextCount == 18 &&
        !UHI::SkyrimInputContextLayoutForVersion(1,6,9999), "Verified SE/AE context layout boundaries");
    std::filesystem::remove_all(root);
    return failures ? 1 : 0;
}
