#pragma once

#include <string>
#include <string_view>
#include <cstdint>

namespace UHI
{
    enum class NumericCodeSpace
    {
        unknown,
        directInputScanCode,
        skseUnifiedInputCode,
        windowsVirtualKey,
        controlMapKeyboard,
        controlMapMouse,
        controlMapGamepad
    };

    struct ParsedConfigBinding
    {
        std::string binding;
        std::string device{ "unknown" };
        std::string codeSystem{ "unknown" };
        bool conflictEligible{ false };
    };

    // Converts the common text notations used in mod configuration files into
    // the physical names drawn on the keyboard/mouse/gamepad boards.
    ParsedConfigBinding ParseConfigBinding(std::string_view settingName, std::string_view rawValue,
        NumericCodeSpace numericHint = NumericCodeSpace::unknown);

    // SKSE/Papyrus uses one continuous macro range: keyboard 0..255,
    // mouse 256..265 and gamepad 266..281. This is deliberately separate
    // from controlmap.txt gamepad masks.
    ParsedConfigBinding ParseSkseInputCode(std::string_view rawValue);

    // controlmap.txt is not one universal DX code column: keyboard is a DIK
    // scan code, mouse is a zero-based button ID and gamepad is a Skyrim/XInput
    // mask. Parse each field with its own rules.
    ParsedConfigBinding ParseControlMapInputCode(std::string_view rawValue, std::string_view device);

    // Win32 GetKeyState/GetAsyncKeyState/RegisterHotKey use VK values, not
    // DirectInput scan codes.
    ParsedConfigBinding ParseVirtualKeyCode(std::uint32_t virtualKey);
}
