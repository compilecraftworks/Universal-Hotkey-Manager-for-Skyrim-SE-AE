#include "UHI/ConfigBindingParser.h"

#include <iostream>

int main()
{
    const auto keyboard = UHI::ParseConfigBinding("HotkeyMenu", "Ctrl+DIK_F10");
    const auto mouse = UHI::ParseConfigBinding("MouseHotkey", "Mouse4");
    const auto gamepad = UHI::ParseConfigBinding("ControllerHotkey", "Gamepad_DPad_Up");
    const auto ambiguous = UHI::ParseConfigBinding("KeyCode", "68");
    const auto numeric = UHI::ParseConfigBinding("ScanCode", "68", UHI::NumericCodeSpace::directInputScanCode);
    const auto mouseCode = UHI::ParseConfigBinding("SkseKey", "259", UHI::NumericCodeSpace::skseUnifiedInputCode);
    const auto gamepadCode = UHI::ParseConfigBinding("SkseKey", "276", UHI::NumericCodeSpace::skseUnifiedInputCode);
    const auto virtualEnd = UHI::ParseConfigBinding("VirtualKey", "35", UHI::NumericCodeSpace::windowsVirtualKey);
    const auto controlMouse = UHI::ParseControlMapInputCode("0", "mouse");
    const auto virtualF10 = UHI::ParseVirtualKeyCode(0x79);
    const auto virtualMouse = UHI::ParseVirtualKeyCode(0x05);
    const auto numericChord = UHI::ParseConfigBinding("ToggleKeys", "0x36+0x0E",
        UHI::NumericCodeSpace::directInputScanCode);
    if (keyboard.binding != "Ctrl+F10" || keyboard.device != "keyboard" || !keyboard.conflictEligible ||
        mouse.binding != "M4" || mouse.device != "mouse" || !mouse.conflictEligible ||
        gamepad.binding != "DUp" || gamepad.device != "gamepad" || !gamepad.conflictEligible ||
        ambiguous.binding != "Numeric 68 (encoding unknown)" || ambiguous.conflictEligible ||
        numeric.binding != "F10" || numeric.device != "keyboard" || !numeric.conflictEligible ||
        mouseCode.binding != "M4" || mouseCode.device != "mouse" || !mouseCode.conflictEligible ||
        gamepadCode.binding != "A" || gamepadCode.device != "gamepad" || !gamepadCode.conflictEligible ||
        virtualEnd.binding != "End" || virtualF10.binding != "F10" || virtualF10.device != "keyboard" || !virtualF10.conflictEligible ||
        controlMouse.binding != "LMB" || controlMouse.codeSystem != "controlmap mouse button ID" ||
        virtualMouse.binding != "M4" || virtualMouse.device != "mouse" || !virtualMouse.conflictEligible ||
        numericChord.binding != "RShift+Bksp" || numericChord.device != "keyboard" ||
        !numericChord.conflictEligible) {
        std::cerr << "Config binding parser test failed\n";
        return 1;
    }
    std::cout << "Config binding parser test passed\n";
    return 0;
}
