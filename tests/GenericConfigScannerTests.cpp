#include "UHI/scanners/GenericConfigScanner.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <vector>

int main(const int argc, char** argv)
{
    if (argc > 1) {
        for (int argument = 1; argument < argc; ++argument) {
            const std::filesystem::path path(argv[argument]);
            const auto records = UHI::Scanners::GenericConfigScanner{}.ScanContent(path,
                [&]() {
                    std::ifstream input(path, std::ios::binary);
                    return std::string((std::istreambuf_iterator<char>(input)), {});
                }());
            std::cout << path.string() << '\t' << records.size() << "\n";
            for (const auto& record : records) {
                std::cout << record.owner << '\t' << record.action << '\t'
                          << record.settingName << '\t' << record.binding << '\t'
                          << record.codeSystem << "\n";
            }
        }
        return 0;
    }
    const auto root = std::filesystem::temp_directory_path() / "uhi_generic_test";
    std::filesystem::create_directories(root);
    const auto activeRoot = root / "SKSE" / "Plugins";
    std::filesystem::create_directories(activeRoot);
    const auto path = activeRoot / "RuntimeMod.ini";
    {
        std::ofstream output(path);
        output << "HotkeyOpen=F7\nKeybindClose=0\niKey=68\nKeyCode=69\nScanCode=68\nVirtualKey=121\nSkseKeyMap=276\nUnrelated=F8\nKeyCustom=F2\nKeyDefine=F3\nPrimaryKeyDefine=F4\n";
    }

    {
        std::ofstream output(activeRoot / "ReShade.ini");
        output << "KeyOverlay=F8\n";
    }
    {
        std::ofstream output(activeRoot / "Plugin_translation.ini");
        output << "averageKey=A\n";
    }
    {
        std::ofstream output(activeRoot / "Skyrim_Default.ini");
        output << "HotkeyShouldNotBeScanned=F1\n";
    }
    {
        std::ofstream output(activeRoot / "ControlMap_Custom.txt");
        output << "HotkeyShouldNotBeScanned=F2\n";
    }
    {
        std::ofstream output(activeRoot / "meta.ini");
        output << "HotkeyShouldBePruned=F3\n";
    }
    {
        std::ofstream output(activeRoot / "Distribution_KID.ini");
        output << "HotkeyShouldBePruned=F3\n";
    }
    {
        std::ofstream output(activeRoot / "README-hotkeys.txt");
        output << "HotkeyShouldBePruned=F4\n";
    }
    std::filesystem::create_directories(activeRoot / "Themes");
    {
        std::ofstream output(activeRoot / "Themes" / "Colors.json");
        output << "{\"Button\":\"#FFFFFFFF\"}\n";
    }
    for (const auto* inactiveDirectory : { "Styles", "Presets", "backup" }) {
        std::filesystem::create_directories(activeRoot / inactiveDirectory);
        std::ofstream output(activeRoot / inactiveDirectory / "Inactive.json");
        output << "{\"HotkeyShouldBePruned\":\"F12\"}\n";
    }
    for (const auto* resourceDirectory : { "Sound", "Textures", "Logs", "Documentation", "Docs" }) {
        std::filesystem::create_directories(activeRoot / resourceDirectory);
        std::ofstream output(activeRoot / resourceDirectory / "Hotkey-looking-metadata.ini");
        output << "HotkeyShouldBePruned=F12\n";
    }

    const std::u16string utf16 = u"{\"action\":\"Photo mode\",\"key\":68,\"ctrl\":true}\n"
        u"<Setting name=\"ScreenshotHotkey\" value=\"69\"/>\n";
    {
        std::ofstream output(activeRoot / "Nested.json", std::ios::binary);
        output.put(static_cast<char>(0xFF));
        output.put(static_cast<char>(0xFE));
        output.write(reinterpret_cast<const char*>(utf16.data()), static_cast<std::streamsize>(utf16.size() * sizeof(char16_t)));
    }
    {
        std::ofstream output(activeRoot / "CustomNames.xml");
        output << "<Config><KeyCustom value=\"F5\"/><Setting name=\"KeyDefine\" value=\"F6\"/>"
                  "<Setting name=\"PrimaryKeyDefine\" value=\"F7\"/></Config>\n";
    }

    const auto records = UHI::Scanners::GenericConfigScanner{}.Scan(root);
    const auto mcmFallback = UHI::Scanners::GenericConfigScanner{}.ScanContent(
        root / "MCM" / "Settings" / "Example.ini", "KeyCode=35\n");
    const auto rootFallback = UHI::Scanners::GenericConfigScanner{}.ScanContent(
        root / "NativeRoot.ini", "KeyCode=35\n", {}, UHI::NumericCodeSpace::windowsVirtualKey);
    const auto improvedCamera = UHI::Scanners::GenericConfigScanner{}.ScanContent(
        root / "SKSE" / "Plugins" / "ImprovedCameraSE" / "ImprovedCameraSE.ini", "MenuKey=0x24\n");
    const auto displayTweaks = UHI::Scanners::GenericConfigScanner{}.ScanContent(
        root / "SKSE" / "Plugins" / "SSEDisplayTweaks.ini", "ComboKey=1\nToggleKey=0xD2\n");
    const auto equipmentDisplays = UHI::Scanners::GenericConfigScanner{}.ScanContent(
        root / "SKSE" / "Plugins" / "ImmersiveEquipmentDisplays.ini", "ToggleKeys=0x36+0x0E\n");
    const auto wheeler = UHI::Scanners::GenericConfigScanner{}.ScanContent(
        root / "SKSE" / "Plugins" / "wheeler" / "AmmoWheel.ini",
        "ToggleKeyMKB=258\nModifierKeyMKB=0\nToggleKeyGamepad=269\n");
    const auto commentsAndStructures = UHI::Scanners::GenericConfigScanner{}.ScanContent(
        root / "SKSE" / "Plugins" / "Example.json",
        "# ToggleKey=0xD2\n\"keys\": {\n\"CurrentHotkey\": [1.0, 0.5]\nOverrideToggleKeys=true\nMenuKey=87\n");
    const auto bareHotkey = UHI::Scanners::GenericConfigScanner{}.ScanContent(
        root / "SKSE" / "Plugins" / "ExampleMod.ini", "Hotkey=F7\n");
    const auto sectionHotkey = UHI::Scanners::GenericConfigScanner{}.ScanContent(
        root / "MCM" / "Settings" / "PhotoMod.ini", "[Photo mode]\nHotkey=F8\n");
    const auto itemCountNamedKey = UHI::Scanners::GenericConfigScanner{}.ScanContent(
        root / "SKSE" / "Plugins" / "QUI.toml", "[PluginExplorer.Count]\nKey=1\n");
    const auto semanticBareKey = UHI::Scanners::GenericConfigScanner{}.ScanContent(
        root / "SKSE" / "Plugins" / "Action.json", "{\"action\":\"Photo mode\",\"key\":68}\n");
    const auto spacedJsonKey = UHI::Scanners::GenericConfigScanner{}.ScanContent(
        root / "SKSE" / "Plugins" / "Modex" / "user" / "settings.json",
        "{\"Open Menu Keybind\":79}\n");
    const auto semanticKeyNouns = UHI::Scanners::GenericConfigScanner{}.ScanContent(
        root / "SKSE" / "Plugins" / "Items.json",
        "{\"chastityKeyWeight\":50,\"minKeysLooted\":3,\"keywordEditorId\":\"Armor\"}\n");
    const auto storageNamespace = UHI::Scanners::GenericConfigScanner{}.ScanContent(
        root / "SKSE" / "Plugins" / "StorageUtilData" / "UD" / "Presets" / "Default.json",
        "{\"actionkey_keycode\":18}\n", {}, UHI::NumericCodeSpace::skseUnifiedInputCode);
    const auto storageRootFile = UHI::Scanners::GenericConfigScanner{}.ScanContent(
        root / "SKSE" / "Plugins" / "StorageUtilData" / "WetFunction.json",
        "{\"toggle_hotkey\":F7}\n", {}, UHI::NumericCodeSpace::skseUnifiedInputCode);
    const auto typedSelector = UHI::Scanners::GenericConfigScanner{}.ScanContent(
        root / "MCM" / "Settings" / "SexLabTools.ini", "iSelectorHotkey=F7\n");
    const auto describedTypedSelector = UHI::Scanners::GenericConfigScanner{}.ScanContent(
        root / "MCM" / "Settings" / "AnimationPicker.ini",
        "# Open animation selector\niSelectorHotkey=F8\n");
    const auto dkaf = UHI::Scanners::GenericConfigScanner{}.ScanContent(
        root / "SKSE" / "Plugins" / "DKAF" / "IdleStop.json",
        "{\"IdleStopW\":{\"Keys\":\"17\",\"GamepadKeys\":\"266\","
        "\"ActionOrIdle\":\"BowRapidCombo.esp|0x829\"}}\n");
    const auto mcmKeybinds = UHI::Scanners::GenericConfigScanner{}.ScanContent(
        root / "MCM" / "Config" / "DynamicArmorMenu" / "keybinds.json",
        "{\"modName\":\"DynamicArmorMenu\",\"ToggleKey\":\"F7\"}\n", "keybinds");
    const auto defeatConfig = UHI::Scanners::GenericConfigScanner{}.ScanContent(
        root / "SKSE" / "Plugins" / "Defeat" / "DefeatConfig.json",
        "{\"int\":{\"hkoption\":60,\"hkaction\":34,\"hkmodifier\":42}}\n");
    std::filesystem::remove_all(root);
    const auto has = [&](const std::string_view action, const std::string_view binding) {
        return std::ranges::any_of(records, [&](const auto& record) {
            return record.action == action && record.binding == binding;
        });
    };
    if (UHI::Scanners::GenericConfigScanner::MayContainBinding("keyboard gamepad controller layout") ||
        !UHI::Scanners::GenericConfigScanner::MayContainBinding("MenuKey=87") ||
        records.size() != 14 || !has("Open", "F7") || !has("Runtime Mod", "F2") ||
        !has("Runtime Mod", "F3") || !has("Runtime Mod", "F4") ||
        !has("Custom Names", "F5") || !has("Custom Names", "F6") || !has("Custom Names", "F7") ||
        !has("Runtime Mod", "F10") || !has("Runtime Mod", "NumLk") ||
        !has("Runtime Mod", "F10") || !has("Skse", "A") ||
        !has("Photo mode", "Ctrl+F10") ||
        !has("Screenshot", "NumLk") ||
        mcmFallback.size() != 1 || mcmFallback[0].binding != "H" ||
        mcmFallback[0].codeSystem.find("source-convention fallback") == std::string::npos ||
        rootFallback.size() != 1 || rootFallback[0].binding != "End" ||
        improvedCamera.size() != 1 || improvedCamera[0].binding != "LShift+Home" ||
        displayTweaks.size() != 1 || displayTweaks[0].binding != "LShift+Ins" ||
        equipmentDisplays.size() != 1 || equipmentDisplays[0].binding != "RShift+Bksp" ||
        wheeler.size() != 2 || wheeler[0].binding != "MMB" || wheeler[1].binding != "DRight" ||
        commentsAndStructures.size() != 1 || commentsAndStructures[0].binding != "F11" ||
        bareHotkey.size() != 1 || bareHotkey[0].action != "Example Mod" ||
        sectionHotkey.size() != 1 || sectionHotkey[0].action != "Photo mode" ||
        !itemCountNamedKey.empty() || semanticBareKey.size() != 1 || semanticBareKey[0].action != "Photo mode" ||
        spacedJsonKey.size() != 1 || spacedJsonKey[0].action != "Open Menu" ||
        spacedJsonKey[0].settingName != "Open Menu Keybind" || !spacedJsonKey[0].editable ||
        !semanticKeyNouns.empty() ||
        storageNamespace.size() != 1 || storageNamespace[0].owner != "UD" ||
        storageRootFile.size() != 1 || storageRootFile[0].owner != "Wet Function" ||
        typedSelector.size() != 1 || typedSelector[0].action != "Sex Lab Tools Selector" ||
        describedTypedSelector.size() != 1 ||
        describedTypedSelector[0].action != "Open animation selector" ||
        dkaf.size() != 2 || dkaf[0].owner != "Bow Rapid Combo" ||
        dkaf[0].action != "Cancel Bow Combo - Forward" ||
        !std::ranges::any_of(dkaf, [](const auto& record) {
            return record.settingName == "Keys" && record.binding == "W" &&
                record.device == "keyboard";
        }) ||
        !std::ranges::any_of(dkaf, [](const auto& record) {
            return record.settingName == "GamepadKeys" && record.binding == "DUp" &&
                record.device == "gamepad";
        }) ||
        mcmKeybinds.size() != 1 || mcmKeybinds[0].owner != "Dynamic Armor Variants" ||
        !std::ranges::any_of(defeatConfig, [](const auto& record) {
            return record.settingName == "hkoption" && record.binding == "F2" && record.editable;
        })) {
        std::cerr << "Generic config scanner test failed: count=" << records.size();
        if (!records.empty()) std::cerr << " binding=" << records[0].binding;
        for (const auto& record : records) {
            std::cerr << "\n  " << record.owner << " | " << record.action << " | "
                      << record.settingName << " | " << record.binding;
        }
        const auto dump = [](const char* name, const auto& values) {
            std::cerr << "\n  [" << name << "] count=" << values.size();
            for (const auto& value : values) {
                std::cerr << "\n    " << value.owner << " | " << value.action << " | "
                          << value.settingName << " | " << value.binding << " | " << value.codeSystem;
            }
        };
        dump("mcmFallback", mcmFallback);
        dump("rootFallback", rootFallback);
        dump("improvedCamera", improvedCamera);
        dump("displayTweaks", displayTweaks);
        dump("equipmentDisplays", equipmentDisplays);
        dump("wheeler", wheeler);
        dump("commentsAndStructures", commentsAndStructures);
        dump("bareHotkey", bareHotkey);
        dump("sectionHotkey", sectionHotkey);
        dump("semanticBareKey", semanticBareKey);
        dump("spacedJsonKey", spacedJsonKey);
        dump("storageNamespace", storageNamespace);
        dump("storageRootFile", storageRootFile);
        dump("typedSelector", typedSelector);
        dump("describedTypedSelector", describedTypedSelector);
        dump("dkaf", dkaf);
        dump("mcmKeybinds", mcmKeybinds);
        dump("defeatConfig", defeatConfig);
        std::cerr << "\n";
        return 1;
    }
    std::cout << "Generic config scanner test passed\n";
    return 0;
}
