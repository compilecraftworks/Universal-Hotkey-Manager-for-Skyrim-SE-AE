#include "UHI/InputCodeFormatter.h"

#include <array>
#include <string>
#include <unordered_map>

namespace
{
    const std::unordered_map<std::uint32_t, std::string_view>& DirectInputKeys()
    {
        static const std::unordered_map<std::uint32_t, std::string_view> keys{
            { 0x01, "Esc" }, { 0x02, "1" }, { 0x03, "2" }, { 0x04, "3" }, { 0x05, "4" },
            { 0x06, "5" }, { 0x07, "6" }, { 0x08, "7" }, { 0x09, "8" }, { 0x0A, "9" },
            { 0x0B, "0" }, { 0x0C, "-" }, { 0x0D, "=" }, { 0x0E, "Bksp" }, { 0x0F, "Tab" },
            { 0x10, "Q" }, { 0x11, "W" }, { 0x12, "E" }, { 0x13, "R" }, { 0x14, "T" },
            { 0x15, "Y" }, { 0x16, "U" }, { 0x17, "I" }, { 0x18, "O" }, { 0x19, "P" },
            { 0x1A, "[" }, { 0x1B, "]" }, { 0x1C, "Ent" }, { 0x1D, "LCtrl" },
            { 0x1E, "A" }, { 0x1F, "S" }, { 0x20, "D" }, { 0x21, "F" }, { 0x22, "G" },
            { 0x23, "H" }, { 0x24, "J" }, { 0x25, "K" }, { 0x26, "L" }, { 0x27, ";" },
            { 0x28, "'" }, { 0x29, "`" }, { 0x2A, "LShift" }, { 0x2B, "\\" },
            { 0x2C, "Z" }, { 0x2D, "X" }, { 0x2E, "C" }, { 0x2F, "V" }, { 0x30, "B" },
            { 0x31, "N" }, { 0x32, "M" }, { 0x33, "," }, { 0x34, "." }, { 0x35, "/" },
            { 0x36, "RShift" }, { 0x37, "Num*" }, { 0x38, "LAlt" }, { 0x39, "Space" },
            { 0x3A, "Caps" }, { 0x3B, "F1" }, { 0x3C, "F2" }, { 0x3D, "F3" },
            { 0x3E, "F4" }, { 0x3F, "F5" }, { 0x40, "F6" }, { 0x41, "F7" },
            { 0x42, "F8" }, { 0x43, "F9" }, { 0x44, "F10" }, { 0x45, "NumLk" },
            { 0x46, "ScrLk" }, { 0x47, "Num7" }, { 0x48, "Num8" }, { 0x49, "Num9" },
            { 0x4A, "Num-" }, { 0x4B, "Num4" }, { 0x4C, "Num5" }, { 0x4D, "Num6" },
            { 0x4E, "Num+" }, { 0x4F, "Num1" }, { 0x50, "Num2" }, { 0x51, "Num3" },
            { 0x52, "Num0" }, { 0x53, "Num." }, { 0x57, "F11" }, { 0x58, "F12" },
            { 0x8D, "Num=" }, { 0x90, "PrevTrack" }, { 0x99, "NextTrack" },
            { 0x9C, "NumEnt" }, { 0x9D, "RCtrl" }, { 0xA0, "Mute" },
            { 0xA1, "Calculator" }, { 0xA2, "PlayPause" }, { 0xA4, "MediaStop" },
            { 0xAE, "VolDown" }, { 0xB0, "VolUp" }, { 0xB2, "WebHome" },
            { 0xB3, "Num," }, { 0xB5, "Num/" }, { 0xB7, "PrtSc" }, { 0xB8, "RAlt" },
            { 0xC5, "Pause" }, { 0xC7, "Home" }, { 0xC8, "Up" }, { 0xC9, "PgUp" },
            { 0xCB, "Left" }, { 0xCD, "Right" }, { 0xCF, "End" }, { 0xD0, "Down" },
            { 0xD1, "PgDn" }, { 0xD2, "Ins" }, { 0xD3, "Del" }, { 0xDB, "LWin" },
            { 0xDC, "RWin" }, { 0xDD, "Menu" }, { 0xDE, "Power" }, { 0xDF, "Sleep" },
            { 0xE3, "Wake" }, { 0xE5, "WebSearch" }, { 0xE6, "WebFavorites" },
            { 0xE7, "WebRefresh" }, { 0xE8, "WebStop" }, { 0xE9, "WebForward" },
            { 0xEA, "WebBack" }, { 0xEB, "MyComputer" }, { 0xEC, "Mail" },
            { 0xED, "MediaSelect" }
        };
        return keys;
    }

    std::string ExactGamepadButton(const std::uint32_t code)
    {
        // Skyrim reserves 0x0009..0x000C for triggers/sticks. Test these before
        // attempting to decompose an XInput mask (0x0009 would otherwise look
        // like DUp+DRight).
        static const std::unordered_map<std::uint32_t, std::string_view> buttons{
            { 0x0001, "DUp" }, { 0x0002, "DDown" }, { 0x0004, "DLeft" },
            { 0x0008, "DRight" }, { 0x0009, "LT" }, { 0x000A, "RT" },
            { 0x000B, "Left Stick" }, { 0x000C, "Right Stick" }, { 0x0010, "Start" },
            { 0x0020, "Back" }, { 0x0040, "Left Stick Click" }, { 0x0080, "Right Stick Click" },
            { 0x0100, "LB" }, { 0x0200, "RB" }, { 0x1000, "A" },
            { 0x2000, "B" }, { 0x4000, "X" }, { 0x8000, "Y" }
        };
        if (const auto found = buttons.find(code); found != buttons.end()) return std::string(found->second);
        return {};
    }
}

namespace UHI
{
    std::string FormatDirectInputScanCode(const std::uint32_t code)
    {
        if (const auto found = DirectInputKeys().find(code); found != DirectInputKeys().end()) {
            return std::string(found->second);
        }
        return {};
    }

    std::string FormatSkseMouseCode(const std::uint32_t code)
    {
        static constexpr std::array<std::string_view, 10> buttons{
            "LMB", "RMB", "MMB", "M4", "M5", "M6", "M7", "M8", "WheelUp", "WheelDown"
        };
        if (code < 256 || code >= 256 + buttons.size()) return {};
        return std::string(buttons[code - 256]);
    }

    std::string FormatSkseGamepadCode(const std::uint32_t code)
    {
        static constexpr std::array<std::string_view, 16> buttons{
            "DUp", "DDown", "DLeft", "DRight", "Start", "Back", "Left Stick Click", "Right Stick Click",
            "LB", "RB", "A", "B", "X", "Y", "LT", "RT"
        };
        if (code < 266 || code >= 266 + buttons.size()) return {};
        return std::string(buttons[code - 266]);
    }

    std::string FormatControlMapMouseCode(const std::uint32_t code)
    {
        static constexpr std::array<std::string_view, 11> buttons{
            "LMB", "RMB", "MMB", "M4", "M5", "M6", "M7", "M8", "WheelUp", "WheelDown", "MouseMove"
        };
        return code < buttons.size() ? std::string(buttons[code]) : std::string{};
    }

    std::string FormatControlMapGamepadCode(const std::uint32_t code)
    {
        if (auto exact = ExactGamepadButton(code); !exact.empty()) return exact;

        static constexpr std::array<std::pair<std::uint32_t, std::string_view>, 14> masks{
            std::pair{ 0x0001U, "DUp" }, { 0x0002U, "DDown" }, { 0x0004U, "DLeft" },
            { 0x0008U, "DRight" }, { 0x0010U, "Start" }, { 0x0020U, "Back" },
            { 0x0040U, "Left Stick Click" }, { 0x0080U, "Right Stick Click" }, { 0x0100U, "LB" },
            { 0x0200U, "RB" }, { 0x1000U, "A" }, { 0x2000U, "B" },
            { 0x4000U, "X" }, { 0x8000U, "Y" }
        };
        std::uint32_t remaining = code;
        std::string result;
        for (const auto& [mask, name] : masks) {
            if ((remaining & mask) == 0) continue;
            if (!result.empty()) result += '+';
            result += name;
            remaining &= ~mask;
        }
        return remaining == 0 ? result : std::string{};
    }
}
