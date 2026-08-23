#include "UHI/BindingSerializer.h"
#include "UHI/ConfigBindingParser.h"
#include "UHI/InputCodeFormatter.h"
#include "UHI/scanners/CommunityShadersScanner.h"
#include "UHI/scanners/ReShadeScanner.h"
#include "UHI/writers/ConfigFileWriter.h"
#include "UHI/writers/ControlMapWriter.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>

namespace
{
    UHI::HotkeyRecord Record(std::string system, std::string raw)
    {
        return { .rawBinding = std::move(raw), .codeSystem = std::move(system), .editable = true };
    }

    bool BindingIs(const UHI::SerializedBinding& value, const std::string_view display,
        const std::string_view raw)
    {
        if (value.error.empty() && value.display == display && value.raw == raw) return true;
        std::cerr << "serialization mismatch: display=[" << value.display << "] raw=[" << value.raw
                  << "] error=[" << value.error << "]\n";
        return false;
    }
}

int main()
{
    bool ok = true;

    const auto direct = UHI::SerializeCapturedBinding(Record("DirectInput scan code", "0x3B"),
        "keyboard", 0x3FU); // F5
    ok = BindingIs(direct, "F5", "0x3F") && ok;
    ok = UHI::ParseConfigBinding("Key", direct.raw, UHI::NumericCodeSpace::directInputScanCode).binding == "F5" && ok;

    const auto skseKeyboard = UHI::SerializeCapturedBinding(Record("SKSE unified input code", "63"),
        "keyboard", 0x41U); // F7
    const auto skseMouse = UHI::SerializeCapturedBinding(Record("SKSE unified input code", "256"),
        "mouse", 3U); // M4
    const auto sksePad = UHI::SerializeCapturedBinding(Record("SKSE unified input code", "276"),
        "gamepad", 281U); // RT
    ok = BindingIs(skseKeyboard, "F7", "65") && ok;
    ok = BindingIs(skseMouse, "M4", "259") && ok;
    ok = BindingIs(sksePad, "RT", "281") && ok;
    ok = UHI::ParseSkseInputCode(skseKeyboard.raw).binding == "F7" && ok;
    ok = UHI::ParseSkseInputCode(skseMouse.raw).binding == "M4" && ok;
    ok = UHI::ParseSkseInputCode(sksePad.raw).binding == "RT" && ok;
    ok = !UHI::SerializeCapturedBinding(Record("SKSE unified input code", "63"),
        "keyboard", 0x41U, "keyboard", 0x2AU).error.empty() && ok;

    const auto vk = UHI::SerializeCapturedBinding(Record("Windows virtual-key code", "0x70"),
        "keyboard", 0x57U); // F11 -> VK_F11
    const auto reshade = UHI::SerializeCapturedBinding(
        Record("ReShade tuple (Windows virtual-key + modifier flags)", "36,0,0,0"),
        "keyboard", 0xC7U, "keyboard", 0x2AU); // Shift+Home
    const auto enb = UHI::SerializeCapturedBinding(Record("ENB decimal Windows virtual-key code", "121"),
        "keyboard", 0x44U); // F10
    const auto enbChord = UHI::SerializeCapturedBinding(Record("ENB decimal Windows virtual-key code", "121"),
        "keyboard", 0x44U, "keyboard", 0x1DU);
    auto enbExistingChordRecord = Record("ENB decimal Windows virtual-key code", "121");
    enbExistingChordRecord.binding = "Shift+F10";
    const auto enbPreservedChord = UHI::SerializeCapturedBinding(enbExistingChordRecord,
        "keyboard", 0x43U); // preserve shared Shift while changing main key to F9
    const auto enbMatchingChord = UHI::SerializeCapturedBinding(enbExistingChordRecord,
        "keyboard", 0x43U, "keyboard", 0x36U);
    const auto enbDifferentChord = UHI::SerializeCapturedBinding(enbExistingChordRecord,
        "keyboard", 0x43U, "keyboard", 0x1DU);
    ok = BindingIs(vk, "F11", "0x7a") && ok;
    ok = BindingIs(reshade, "Shift+Home", "36,0,1,0") && ok;
    ok = BindingIs(enb, "F10", "121") && ok;
    ok = !enbChord.error.empty() && ok;
    ok = BindingIs(enbPreservedChord, "Shift+F9", "120") && ok;
    ok = BindingIs(enbMatchingChord, "Shift+F9", "120") && ok;
    ok = !enbDifferentChord.error.empty() && ok;
    ok = UHI::ParseVirtualKeyCode(0x7A).binding == "F11" && ok;
    ok = UHI::Scanners::FormatReShadeBinding(reshade.raw) == "Shift+Home" && ok;

    const auto csKeyboard = UHI::SerializeCapturedBinding(
        Record("Community Shaders InputCombo (Windows VK / packed device)", "112"),
        "keyboard", 0x57U);
    const auto csMouse = UHI::SerializeCapturedBinding(
        Record("Community Shaders InputCombo (Windows VK / packed device)", "[262145]"),
        "mouse", 3U);
    const auto csPad = UHI::SerializeCapturedBinding(
        Record("Community Shaders InputCombo (Windows VK / packed device)", "[331776]"),
        "gamepad", 276U);
    const auto csChord = UHI::SerializeCapturedBinding(
        Record("Community Shaders InputCombo (Windows VK / packed device)", "[17,112]"),
        "keyboard", 0x57U, "keyboard", 0x1DU);
    ok = BindingIs(csKeyboard, "F11", "122") && ok;
    ok = BindingIs(csMouse, "M4", "[262149]") && ok;
    ok = BindingIs(csPad, "A", "[331776]") && ok;
    ok = BindingIs(csChord, "LCtrl+F11", "[162,122]") && ok;

    const auto cmKeyboard = UHI::SerializeCapturedBinding(
        Record("controlmap keyboard DirectInput scan code", "0x11"), "keyboard", 0x20U);
    const auto cmMouse = UHI::SerializeCapturedBinding(
        Record("controlmap mouse button ID", "0x0"), "mouse", 4U);
    const auto cmPad = UHI::SerializeCapturedBinding(
        Record("controlmap Skyrim/XInput mask", "0x0001"), "gamepad", 281U);
    ok = BindingIs(cmKeyboard, "D", "0x20") && ok;
    ok = BindingIs(cmMouse, "M5", "0x4") && ok;
    ok = BindingIs(cmPad, "RT", "0x000a") && ok;
    ok = UHI::ParseControlMapInputCode(cmKeyboard.raw, "keyboard").binding == "D" && ok;
    ok = UHI::ParseControlMapInputCode(cmMouse.raw, "mouse").binding == "M5" && ok;
    ok = UHI::ParseControlMapInputCode(cmPad.raw, "gamepad").binding == "RT" && ok;

    const auto dikSymbol = UHI::SerializeCapturedBinding(Record("DirectInput symbol", "Ctrl+DIK_F10"),
        "keyboard", 0x57U, "keyboard", 0x1DU);
    const auto vkSymbol = UHI::SerializeCapturedBinding(Record("Windows virtual-key symbol", "VK_F1"),
        "keyboard", 0x57U);
    const auto padSymbol = UHI::SerializeCapturedBinding(Record("symbolic key name", "Gamepad_A"),
        "gamepad", 279U);
    ok = BindingIs(dikSymbol, "LCtrl+F11", "Ctrl+DIK_F11") && ok;
    ok = BindingIs(vkSymbol, "F11", "VK_F11") && ok;
    ok = BindingIs(padSymbol, "Y", "GAMEPAD_Y") && ok;
    ok = UHI::ParseConfigBinding("Hotkey", dikSymbol.raw).binding == "Ctrl+F11" && ok;
    ok = UHI::ParseConfigBinding("Hotkey", vkSymbol.raw).binding == "F11" && ok;
    ok = UHI::ParseConfigBinding("GamepadHotkey", padSymbol.raw).binding == "Y" && ok;

    // Every physical key that UHI can display must round-trip in the lossless
    // DirectInput/SKSE formats. Windows VK is allowed to reject keys that the
    // format cannot distinguish, but every accepted value must parse back to
    // the exact physical key shown to the user.
    for (std::uint32_t code = 1U; code < 256U; ++code) {
        const auto expected = UHI::FormatDirectInputScanCode(code);
        if (expected.empty()) continue;
        const auto di = UHI::SerializeCapturedBinding(Record("DirectInput scan code", "0"),
            "keyboard", code);
        const auto unified = UHI::SerializeCapturedBinding(Record("SKSE unified input code", "0"),
            "keyboard", code);
        ok = BindingIs(di, expected, std::to_string(code)) && ok;
        ok = BindingIs(unified, expected, std::to_string(code)) && ok;
        ok = UHI::ParseConfigBinding("Hotkey", di.raw,
            UHI::NumericCodeSpace::directInputScanCode).binding == expected && ok;
        ok = UHI::ParseSkseInputCode(unified.raw).binding == expected && ok;

        const auto windows = UHI::SerializeCapturedBinding(Record("Windows virtual-key code", "0"),
            "keyboard", code);
        if (windows.error.empty()) {
            const auto value = static_cast<std::uint32_t>(std::stoul(windows.raw, nullptr, 0));
            ok = UHI::ParseVirtualKeyCode(value).binding == expected && ok;
        }
    }
    for (std::uint32_t code = 0U; code < 10U; ++code) {
        const auto expected = UHI::FormatSkseMouseCode(256U + code);
        const auto serialized = UHI::SerializeCapturedBinding(Record("SKSE unified input code", "256"),
            "mouse", code);
        ok = serialized.error.empty() && serialized.display == expected &&
            UHI::ParseSkseInputCode(serialized.raw).binding == expected && ok;
    }
    for (std::uint32_t code = 266U; code < 282U; ++code) {
        const auto expected = UHI::FormatSkseGamepadCode(code);
        const auto unified = UHI::SerializeCapturedBinding(Record("SKSE unified input code", "266"),
            "gamepad", code);
        const auto control = UHI::SerializeCapturedBinding(Record("controlmap Skyrim/XInput mask", "0x0001"),
            "gamepad", code);
        ok = unified.error.empty() && unified.display == expected &&
            UHI::ParseSkseInputCode(unified.raw).binding == expected && ok;
        ok = control.error.empty() && control.display == expected &&
            UHI::ParseControlMapInputCode(control.raw, "gamepad").binding == expected && ok;
    }
    // Explicitly cover the lossy VK case and invalid cross-device writes.
    ok = !UHI::SerializeCapturedBinding(Record("Windows virtual-key code", "13"),
        "keyboard", 0x9CU).error.empty() && ok; // Numpad Enter becomes VK_RETURN.
    ok = !UHI::SerializeCapturedBinding(Record("DirectInput scan code", "30"),
        "gamepad", 276U).error.empty() && ok;
    ok = !UHI::SerializeCapturedBinding(Record("controlmap Skyrim/XInput mask", "0x1000"),
        "keyboard", 0x1EU).error.empty() && ok;

    // Exercise serialization through the real transactional writers and scan
    // the resulting files again. This catches correct-looking values that the
    // corresponding source scanner cannot actually read back.
    const auto root = std::filesystem::temp_directory_path() / "uhi_binding_serializer_test";
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root);
    const auto config = root / "Settings.ini";
    { std::ofstream out(config); out << "[Input]\nHotkey=63\n"; }
    ok = UHI::Writers::ConfigFileWriter{}.SetBinding(config, 2, "Hotkey", "63", skseKeyboard.raw) && ok;
    std::ifstream configIn(config);
    const std::string configText((std::istreambuf_iterator<char>(configIn)), {});
    ok = configText.find("Hotkey=65") != std::string::npos && ok;

    const auto csPath = root / "CommunityShaders.json";
    { std::ofstream out(csPath); out << "{\"ToggleKey\":112,\"EffectToggleKey\":[17,112]}\n"; }
    ok = UHI::Writers::ConfigFileWriter{}.SetBinding(csPath, 1, "EffectToggleKey", "[17,112]", csChord.raw) && ok;
    const auto csRecords = UHI::Scanners::CommunityShadersScanner{}.Scan(csPath);
    ok = csRecords.size() == 2U && csRecords[1].binding == "LCtrl+F11" && ok;

    const auto controlMap = root / "controlmap.txt";
    { std::ofstream out(controlMap); out << "Activate\t\t18\t\t2\t7\t\t0\t0\t0\n"; }
    ok = UHI::Writers::ControlMapWriter{}.SetBinding(controlMap, 1, "keyboard", cmKeyboard.raw) && ok;
    ok = UHI::Writers::ControlMapWriter{}.SetBinding(controlMap, 1, "mouse", cmMouse.raw) && ok;
    ok = UHI::Writers::ControlMapWriter{}.SetBinding(controlMap, 1, "gamepad", cmPad.raw) && ok;
    std::ifstream mapIn(controlMap);
    const std::string mapText((std::istreambuf_iterator<char>(mapIn)), {});
    ok = mapText.find("0x20") != std::string::npos && mapText.find("0x4") != std::string::npos &&
        mapText.find("0x000a") != std::string::npos && ok;

    configIn.close();
    mapIn.close();
    std::filesystem::remove_all(root);
    if (!ok) {
        std::cerr << "Binding serializer round-trip test failed\n";
        return 1;
    }
    std::cout << "Binding serializer round-trip test passed\n";
    return 0;
}
