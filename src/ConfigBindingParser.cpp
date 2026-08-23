#include "UHI/ConfigBindingParser.h"
#include "UHI/InputCodeFormatter.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <cctype>
#include <string>

namespace
{
    std::string Upper(std::string_view value)
    {
        std::string result(value);
        std::ranges::transform(result, result.begin(), [](const unsigned char ch) {
            return static_cast<char>(std::toupper(ch));
        });
        return result;
    }

    void ReplaceAll(std::string& value, const std::string_view from, const std::string_view to)
    {
        std::size_t offset{};
        while ((offset = value.find(from, offset)) != std::string::npos) {
            value.replace(offset, from.size(), to);
            offset += to.size();
        }
    }

    bool ParseUnsigned(const std::string_view raw, std::uint32_t& result)
    {
        auto value = raw;
        while (!value.empty() && std::isspace(static_cast<unsigned char>(value.front()))) value.remove_prefix(1);
        while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back()))) value.remove_suffix(1);
        const bool hex = value.size() > 2 && value[0] == '0' && (value[1] == 'x' || value[1] == 'X');
        if (hex) value.remove_prefix(2);
        if (value.empty()) return false;
        const auto [last, error] = std::from_chars(value.data(), value.data() + value.size(), result, hex ? 16 : 10);
        return error == std::errc{} && last == value.data() + value.size();
    }

    bool IsNumeric(const std::string_view value)
    {
        std::uint32_t ignored{};
        return ParseUnsigned(value, ignored);
    }

    std::string KeyboardName(std::string key)
    {
        if (key.starts_with("DIK_")) key.erase(0, 4);
        if (key.starts_with("VK_")) key.erase(0, 3);
        static constexpr std::array<std::pair<std::string_view, std::string_view>, 74> aliases{{
            std::pair{ "ESCAPE", "Esc" }, { "RETURN", "Ent" }, { "ENTER", "Ent" },
            { "BACK", "Bksp" }, { "BACKSPACE", "Bksp" }, { "SPACEBAR", "Space" },
            { "SPACE", "Space" }, { "PRIOR", "PgUp" }, { "NEXT", "PgDn" },
            { "PAGEUP", "PgUp" }, { "PAGEDOWN", "PgDn" }, { "INSERT", "Ins" },
            { "DELETE", "Del" }, { "SNAPSHOT", "PrtSc" }, { "PRINTSCREEN", "PrtSc" },
            { "SCROLLLOCK", "ScrLk" }, { "NUMLOCK", "NumLk" }, { "CAPITAL", "Caps" },
            { "LCONTROL", "LCtrl" }, { "RCONTROL", "RCtrl" }, { "CONTROL", "Ctrl" },
            { "LSHIFT", "LShift" }, { "RSHIFT", "RShift" }, { "LMENU", "LAlt" },
            { "RMENU", "RAlt" }, { "MENU", "Alt" }, { "LWIN", "LWin" }, { "RWIN", "RWin" },
            { "OEM_3", "`" }, { "OEM_MINUS", "-" }, { "OEM_PLUS", "=" },
            { "MULTIPLY", "Num*" }, { "ADD", "Num+" }, { "SUBTRACT", "Num-" }
            , { "DIVIDE", "Num/" }, { "DECIMAL", "Num." },
            { "NUMPAD0", "Num0" }, { "NUMPAD1", "Num1" }, { "NUMPAD2", "Num2" },
            { "NUMPAD3", "Num3" }, { "NUMPAD4", "Num4" }, { "NUMPAD5", "Num5" },
            { "NUMPAD6", "Num6" }, { "NUMPAD7", "Num7" }, { "NUMPAD8", "Num8" },
            { "NUMPAD9", "Num9" }, { "NUMPADENTER", "NumEnt" }, { "NUMPADEQUALS", "Num=" },
            { "TAB", "Tab" }, { "HOME", "Home" }, { "END", "End" }, { "UP", "Up" },
            { "DOWN", "Down" }, { "LEFT", "Left" }, { "RIGHT", "Right" },
            { "PAUSE", "Pause" }, { "APPS", "Menu" }, { "VOLUME_MUTE", "Mute" },
            { "VOLUME_DOWN", "VolDown" }, { "VOLUME_UP", "VolUp" },
            { "MEDIA_PREV_TRACK", "PrevTrack" }, { "MEDIA_NEXT_TRACK", "NextTrack" },
            { "MEDIA_PLAY_PAUSE", "PlayPause" }, { "MEDIA_STOP", "MediaStop" },
            { "BROWSER_HOME", "WebHome" }, { "BROWSER_SEARCH", "WebSearch" },
            { "BROWSER_FAVORITES", "WebFavorites" }, { "BROWSER_REFRESH", "WebRefresh" },
            { "BROWSER_STOP", "WebStop" }, { "BROWSER_FORWARD", "WebForward" },
            { "BROWSER_BACK", "WebBack" }, { "LAUNCH_APP1", "MyComputer" },
            { "LAUNCH_MAIL", "Mail" }, { "LAUNCH_MEDIA_SELECT", "MediaSelect" }
        }};
        for (const auto& [from, to] : aliases) if (key == from) return std::string(to);
        if (key.size() == 1 || (key.size() >= 2 && key[0] == 'F' &&
            std::ranges::all_of(key.substr(1), [](const unsigned char ch) { return std::isdigit(ch) != 0; }))) {
            return key;
        }
        return {};
    }

    UHI::ParsedConfigBinding ParseTypedNumber(const std::string_view rawValue, const UHI::NumericCodeSpace space)
    {
        std::uint32_t code{};
        if (!ParseUnsigned(rawValue, code)) return {};
        std::string binding;
        std::string device = "unknown";
        std::string system = "unknown numeric encoding";
        switch (space) {
        case UHI::NumericCodeSpace::directInputScanCode:
        case UHI::NumericCodeSpace::controlMapKeyboard:
            binding = UHI::FormatDirectInputScanCode(code);
            device = "keyboard";
            system = space == UHI::NumericCodeSpace::controlMapKeyboard ?
                "controlmap keyboard DirectInput scan code" : "DirectInput scan code";
            break;
        case UHI::NumericCodeSpace::windowsVirtualKey: {
            auto parsed = UHI::ParseVirtualKeyCode(code);
            parsed.codeSystem = "Windows virtual-key code";
            return parsed;
        }
        case UHI::NumericCodeSpace::controlMapMouse:
            binding = UHI::FormatControlMapMouseCode(code);
            device = "mouse";
            system = "controlmap mouse button ID";
            break;
        case UHI::NumericCodeSpace::controlMapGamepad:
            binding = UHI::FormatControlMapGamepadCode(code);
            device = "gamepad";
            system = "controlmap Skyrim/XInput mask";
            break;
        case UHI::NumericCodeSpace::skseUnifiedInputCode:
            return UHI::ParseSkseInputCode(rawValue);
        case UHI::NumericCodeSpace::unknown:
            return { .binding = "Numeric " + std::string(rawValue) + " (encoding unknown)",
                .device = "unknown", .codeSystem = "unknown numeric encoding", .conflictEligible = false };
        }
        if (binding.empty()) {
            return { .binding = std::string(rawValue) + " (unknown in " + system + ')',
                .device = device, .codeSystem = system, .conflictEligible = false };
        }
        return { .binding = std::move(binding), .device = std::move(device),
            .codeSystem = std::move(system), .conflictEligible = true };
    }
}

namespace UHI
{
    ParsedConfigBinding ParseVirtualKeyCode(const std::uint32_t virtualKey)
    {
        const auto keyboard = [](std::string binding, const bool eligible = true) {
            return ParsedConfigBinding{ .binding = std::move(binding), .device = "keyboard",
                .codeSystem = "Windows virtual-key code", .conflictEligible = eligible };
        };
        const auto mouse = [](std::string binding) {
            return ParsedConfigBinding{ .binding = std::move(binding), .device = "mouse",
                .codeSystem = "Windows virtual-key code", .conflictEligible = true };
        };
        if (virtualKey >= 0x30 && virtualKey <= 0x39) return keyboard(std::string(1, static_cast<char>(virtualKey)));
        if (virtualKey >= 0x41 && virtualKey <= 0x5A) return keyboard(std::string(1, static_cast<char>(virtualKey)));
        if (virtualKey >= 0x70 && virtualKey <= 0x87) return keyboard("F" + std::to_string(virtualKey - 0x6F));
        if (virtualKey >= 0x60 && virtualKey <= 0x69) return keyboard("Num" + std::to_string(virtualKey - 0x60));
        switch (virtualKey) {
        case 0x01: return mouse("LMB"); case 0x02: return mouse("RMB"); case 0x04: return mouse("MMB");
        case 0x05: return mouse("M4"); case 0x06: return mouse("M5");
        case 0x08: return keyboard("Bksp"); case 0x09: return keyboard("Tab");
        case 0x0C: return keyboard("Clear"); case 0x0D: return keyboard("Ent");
        case 0x10: return keyboard("Shift", false); case 0x11: return keyboard("Ctrl", false);
        case 0x12: return keyboard("Alt", false); case 0x13: return keyboard("Pause");
        case 0x14: return keyboard("Caps"); case 0x1B: return keyboard("Esc");
        case 0x20: return keyboard("Space"); case 0x21: return keyboard("PgUp");
        case 0x22: return keyboard("PgDn"); case 0x23: return keyboard("End");
        case 0x24: return keyboard("Home"); case 0x25: return keyboard("Left");
        case 0x26: return keyboard("Up"); case 0x27: return keyboard("Right");
        case 0x28: return keyboard("Down"); case 0x29: return keyboard("Select");
        case 0x2B: return keyboard("Execute"); case 0x2C: return keyboard("PrtSc");
        case 0x2D: return keyboard("Ins"); case 0x2E: return keyboard("Del");
        case 0x2F: return keyboard("Help"); case 0x5B: return keyboard("LWin");
        case 0x5C: return keyboard("RWin"); case 0x5D: return keyboard("Apps");
        case 0x5F: return keyboard("Sleep"); case 0x6A: return keyboard("Num*");
        case 0x6B: return keyboard("Num+"); case 0x6C: return keyboard("Separator");
        case 0x6D: return keyboard("Num-"); case 0x6E: return keyboard("Num.");
        case 0x6F: return keyboard("Num/"); case 0x90: return keyboard("NumLk");
        case 0x91: return keyboard("ScrLk"); case 0xA0: return keyboard("LShift");
        case 0xA1: return keyboard("RShift"); case 0xA2: return keyboard("LCtrl");
        case 0xA3: return keyboard("RCtrl"); case 0xA4: return keyboard("LAlt");
        case 0xA5: return keyboard("RAlt"); case 0xA6: return keyboard("BrowserBack");
        case 0xA7: return keyboard("BrowserForward"); case 0xA8: return keyboard("BrowserRefresh");
        case 0xA9: return keyboard("BrowserStop"); case 0xAA: return keyboard("BrowserSearch");
        case 0xAB: return keyboard("BrowserFavorites"); case 0xAC: return keyboard("BrowserHome");
        case 0xAD: return keyboard("Mute"); case 0xAE: return keyboard("VolDown");
        case 0xAF: return keyboard("VolUp"); case 0xB0: return keyboard("NextTrack");
        case 0xB1: return keyboard("PrevTrack"); case 0xB2: return keyboard("MediaStop");
        case 0xB3: return keyboard("PlayPause"); case 0xB4: return keyboard("Mail");
        case 0xB5: return keyboard("MediaSelect"); case 0xB6: return keyboard("App1");
        case 0xB7: return keyboard("App2"); case 0xBA: return keyboard(";");
        case 0xBB: return keyboard("="); case 0xBC: return keyboard(",");
        case 0xBD: return keyboard("-"); case 0xBE: return keyboard(".");
        case 0xBF: return keyboard("/"); case 0xC0: return keyboard("`");
        case 0xDB: return keyboard("["); case 0xDC: return keyboard("\\");
        case 0xDD: return keyboard("]"); case 0xDE: return keyboard("'");
        default: {
            constexpr char digits[] = "0123456789ABCDEF";
            std::string hex(2, '0');
            hex[0] = digits[(virtualKey >> 4U) & 0x0FU]; hex[1] = digits[virtualKey & 0x0FU];
            return { .binding = "VK 0x" + hex + " (unknown)", .device = "unknown",
                .codeSystem = "Windows virtual-key code", .conflictEligible = false };
        }}
    }

    ParsedConfigBinding ParseSkseInputCode(const std::string_view rawValue)
    {
        std::uint32_t code{};
        if (!ParseUnsigned(rawValue, code) || code >= 282) {
            return { .binding = "SKSE " + std::string(rawValue) + " (unknown)", .device = "unknown",
                .codeSystem = "SKSE unified input code", .conflictEligible = false };
        }
        std::string binding;
        std::string device;
        if (code < 256) { binding = FormatDirectInputScanCode(code); device = "keyboard"; }
        else if (code < 266) { binding = FormatSkseMouseCode(code); device = "mouse"; }
        else { binding = FormatSkseGamepadCode(code); device = "gamepad"; }
        if (binding.empty()) {
            return { .binding = "SKSE " + std::string(rawValue) + " (unknown)", .device = device,
                .codeSystem = "SKSE unified input code", .conflictEligible = false };
        }
        return { .binding = std::move(binding), .device = std::move(device),
            .codeSystem = "SKSE unified input code", .conflictEligible = true };
    }

    ParsedConfigBinding ParseControlMapInputCode(const std::string_view rawValue, const std::string_view device)
    {
        const auto space = device == "keyboard" ? NumericCodeSpace::controlMapKeyboard :
            device == "mouse" ? NumericCodeSpace::controlMapMouse :
            device == "gamepad" ? NumericCodeSpace::controlMapGamepad : NumericCodeSpace::unknown;
        return ParseTypedNumber(rawValue, space);
    }

    ParsedConfigBinding ParseConfigBinding(const std::string_view settingName, const std::string_view rawValue,
        NumericCodeSpace numericHint)
    {
        const auto name = Upper(settingName);
        auto value = Upper(rawValue);
        value.erase(std::remove_if(value.begin(), value.end(), [](const unsigned char ch) {
            return std::isspace(ch) != 0;
        }), value.end());
        if (value.empty()) return {};

        if (IsNumeric(value)) return ParseTypedNumber(rawValue, numericHint);

        // Several CommonLib/SKSE plugins serialize a chord as multiple numeric
        // codes (for example IED uses 0x36+0x0E for RShift+Backspace). Parse
        // every member in the same declared code space instead of treating the
        // whole value as an unknown symbolic name.
        if (value.find('+') != std::string::npos) {
            std::string binding;
            std::string device;
            std::string codeSystem;
            std::size_t offset{};
            bool numericChord = true;
            bool eligible = true;
            while (offset <= value.size()) {
                const auto end = value.find('+', offset);
                const auto part = value.substr(offset,
                    end == std::string::npos ? value.size() - offset : end - offset);
                if (!IsNumeric(part)) {
                    numericChord = false;
                    break;
                }
                const auto parsed = ParseTypedNumber(part, numericHint);
                if (parsed.binding.empty()) {
                    numericChord = false;
                    break;
                }
                if (!binding.empty()) binding.push_back('+');
                binding += parsed.binding;
                if (device.empty()) device = parsed.device;
                else if (device != parsed.device) device = "mixed";
                if (codeSystem.empty()) codeSystem = parsed.codeSystem;
                else if (codeSystem != parsed.codeSystem) codeSystem = "mixed numeric code systems";
                // A lone modifier is intentionally not conflict-eligible, but
                // it becomes meaningful as part of a fully decoded chord.
                eligible = eligible && parsed.binding.find("unknown") == std::string::npos &&
                    parsed.binding.find("encoding") == std::string::npos;
                if (end == std::string::npos) break;
                offset = end + 1;
            }
            if (numericChord) {
                return { .binding = std::move(binding), .device = std::move(device),
                    .codeSystem = std::move(codeSystem), .conflictEligible = eligible };
            }
        }

        const bool mouse = name.find("MOUSE") != std::string::npos || value.find("MOUSE") != std::string::npos ||
            value.find("XBUTTON") != std::string::npos || value == "LMB" || value == "RMB";
        const bool gamepad = name.find("GAMEPAD") != std::string::npos || name.find("CONTROLLER") != std::string::npos ||
            value.find("GAMEPAD") != std::string::npos || value.find("XINPUT") != std::string::npos ||
            value.find("DPAD") != std::string::npos || value == "LB" || value == "RB" || value == "LT" || value == "RT" ||
            value == "LS" || value == "RS" || value == "L3" || value == "R3";

        if (mouse) {
            static constexpr std::array<std::pair<std::string_view, std::string_view>, 12> aliases{{
                std::pair{ "MOUSE1", "LMB" }, { "MOUSE2", "RMB" }, { "MOUSE3", "MMB" },
                { "MOUSE4", "M4" }, { "MOUSE5", "M5" }, { "LEFTMOUSE", "LMB" },
                { "RIGHTMOUSE", "RMB" }, { "MIDDLEMOUSE", "MMB" }, { "XBUTTON1", "M4" },
                { "XBUTTON2", "M5" }, { "LMB", "LMB" }, { "RMB", "RMB" }
            }};
            for (const auto& [from, to] : aliases) if (value == from) {
                return { .binding = std::string(to), .device = "mouse", .codeSystem = "symbolic key name", .conflictEligible = true };
            }
        }

        if (gamepad) {
            if (value.starts_with("DPAD") || value.find("DPAD_") != std::string::npos) {
                if (value.ends_with("UP")) return { "DUp", "gamepad", "symbolic key name", true };
                if (value.ends_with("DOWN")) return { "DDown", "gamepad", "symbolic key name", true };
                if (value.ends_with("LEFT")) return { "DLeft", "gamepad", "symbolic key name", true };
                if (value.ends_with("RIGHT")) return { "DRight", "gamepad", "symbolic key name", true };
                return { "DPad", "gamepad", "symbolic key name", false };
            }
            ReplaceAll(value, "GAMEPAD_", ""); ReplaceAll(value, "XINPUT_", "");
            static constexpr std::array<std::pair<std::string_view, std::string_view>, 16> pads{{
                { "A", "A" }, { "B", "B" }, { "X", "X" }, { "Y", "Y" },
                { "LB", "LB" }, { "RB", "RB" }, { "LT", "LT" }, { "RT", "RT" },
                { "LS", "Left Stick Click" }, { "L3", "Left Stick Click" },
                { "RS", "Right Stick Click" }, { "R3", "Right Stick Click" },
                { "LEFT_THUMB", "Left Stick Click" }, { "RIGHT_THUMB", "Right Stick Click" },
                { "BACK", "Back" }, { "START", "Start" }
            }};
            for (const auto& [key, display] : pads) if (value == key) {
                return { .binding = std::string(display), .device = "gamepad",
                    .codeSystem = "symbolic key name", .conflictEligible = true };
            }
        }

        std::string result;
        std::size_t offset{};
        while (offset <= value.size()) {
            const auto end = value.find('+', offset);
            const auto part = value.substr(offset, end == std::string::npos ? value.size() - offset : end - offset);
            if (part == "CTRL" || part == "CONTROL") result += "Ctrl+";
            else if (part == "SHIFT") result += "Shift+";
            else if (part == "ALT") result += "Alt+";
            else {
                const auto key = KeyboardName(part);
                if (key.empty()) return { .binding = std::string(rawValue), .device = "unknown",
                    .codeSystem = "unknown symbolic encoding", .conflictEligible = false };
                result += key;
            }
            if (end == std::string::npos) break;
            offset = end + 1;
        }
        const std::string system = value.find("VK_") != std::string::npos ? "Windows virtual-key symbol" :
            value.find("DIK_") != std::string::npos ? "DirectInput symbol" : "symbolic key name";
        return { .binding = result, .device = "keyboard", .codeSystem = system, .conflictEligible = true };
    }
}
