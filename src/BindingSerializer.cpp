#include "UHI/BindingSerializer.h"

#include "UHI/ConfigBindingParser.h"
#include "UHI/InputCodeFormatter.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <iomanip>
#include <optional>
#include <ranges>
#include <sstream>
#ifdef _WIN32
#include <Windows.h>
#endif

namespace
{
    std::string LowerAscii(std::string value)
    {
        std::ranges::transform(value, value.begin(), [](const unsigned char ch) {
            return static_cast<char>(std::tolower(ch));
        });
        return value;
    }

    std::string UpperAscii(std::string value)
    {
        std::ranges::transform(value, value.begin(), [](const unsigned char ch) {
            return static_cast<char>(std::toupper(ch));
        });
        return value;
    }

    std::optional<std::uint32_t> DirectInputToVirtualKey(const std::uint32_t scanCode)
    {
#ifdef _WIN32
        switch (scanCode) {
        case 0x9D: return VK_RCONTROL;
        case 0xB8: return VK_RMENU;
        case 0x1D: return VK_LCONTROL;
        case 0x2A: return VK_LSHIFT;
        case 0x36: return VK_RSHIFT;
        case 0x38: return VK_LMENU;
        default: break;
        }
        // DirectInput marks E0-prefixed keys by setting bit 7 (for example
        // Home=0xC7 and Numpad Enter=0x9C). MapVirtualKey expects the actual
        // E0-prefixed scan code, otherwise navigation keys are lost or mapped
        // as their numpad counterparts.
        const auto windowsScanCode = (scanCode & 0x80U) != 0U ?
            (0xE000U | (scanCode & 0x7FU)) : scanCode;
        const auto mapped = MapVirtualKeyExW(windowsScanCode, MAPVK_VSC_TO_VK_EX, GetKeyboardLayout(0));
        if (mapped != 0) return mapped;
#endif
        return std::nullopt;
    }

    std::optional<std::uint32_t> GamepadMacroToMask(const std::uint32_t macro)
    {
        static constexpr std::array<std::uint32_t, 16> masks{
            0x0001U, 0x0002U, 0x0004U, 0x0008U, 0x0010U, 0x0020U, 0x0040U, 0x0080U,
            0x0100U, 0x0200U, 0x1000U, 0x2000U, 0x4000U, 0x8000U, 0x0009U, 0x000AU
        };
        if (macro < 266U || macro >= 266U + masks.size()) return std::nullopt;
        return masks[macro - 266U];
    }

    std::string CapturedDisplay(const std::string_view device, const std::uint32_t code)
    {
        if (device == "keyboard") return UHI::FormatDirectInputScanCode(code);
        if (device == "mouse") return UHI::FormatControlMapMouseCode(code);
        if (device == "gamepad") return UHI::FormatSkseGamepadCode(code);
        return {};
    }

    std::string NumericLike(const std::uint32_t value, const std::string_view example)
    {
        const auto marker = example.find("0x") != std::string_view::npos ? example.find("0x") : example.find("0X");
        if (marker == std::string_view::npos) return std::to_string(value);
        const bool upperPrefix = example.find("0X") != std::string_view::npos;
        const bool upperDigits = upperPrefix ||
            std::ranges::any_of(example, [](const char ch) { return ch >= 'A' && ch <= 'F'; });
        std::size_t width{};
        for (auto index = marker + 2U; index < example.size() &&
            std::isxdigit(static_cast<unsigned char>(example[index])); ++index) ++width;
        std::ostringstream output;
        output << (upperPrefix ? "0X" : "0x") << std::setfill('0') <<
            std::setw(static_cast<int>((std::max)(width, std::size_t{ 1U }))) <<
            (upperDigits ? std::uppercase : std::nouppercase) << std::hex << value;
        return output.str();
    }

    std::optional<std::uint32_t> CapturedCodeForSystem(const UHI::HotkeyRecord& record,
        const std::string_view device, const std::uint32_t code)
    {
        const auto system = LowerAscii(record.codeSystem);
        if (system.find("community shaders") != std::string::npos) {
            if (device == "keyboard") return DirectInputToVirtualKey(code);
            if (device == "mouse") {
                static constexpr std::array<std::uint32_t, 5> mouseVk{ 0x01U, 0x02U, 0x04U, 0x05U, 0x06U };
                if (code < mouseVk.size()) return (4U << 16U) | mouseVk[code];
            }
            if (device == "gamepad") {
                if (const auto mask = GamepadMacroToMask(code)) return (5U << 16U) | *mask;
            }
            return std::nullopt;
        }
        if (system.find("windows") != std::string::npos || system.find("reshade") != std::string::npos ||
            system.find("enb") != std::string::npos) {
            if (device == "keyboard") return DirectInputToVirtualKey(code);
            if (device == "mouse") {
                static constexpr std::array<std::uint32_t, 5> mouseVk{ 0x01U, 0x02U, 0x04U, 0x05U, 0x06U };
                if (code < mouseVk.size()) return mouseVk[code];
            }
            return std::nullopt;
        }
        if (system.find("skse") != std::string::npos) {
            if (device == "keyboard" && code < 256U) return code;
            if (device == "mouse" && code < 10U) return 256U + code;
            if (device == "gamepad" && code >= 266U && code < 282U) return code;
            return std::nullopt;
        }
        if (system.find("controlmap mouse") != std::string::npos)
            return device == "mouse" && code <= 10U ? std::optional<std::uint32_t>(code) : std::nullopt;
        if (system.find("controlmap skyrim") != std::string::npos || system.find("xinput mask") != std::string::npos)
            return device == "gamepad" ? GamepadMacroToMask(code) : std::nullopt;
        if (system.find("directinput") != std::string::npos || system.find("controlmap keyboard") != std::string::npos)
            return device == "keyboard" && code < 0xFFU ? std::optional<std::uint32_t>(code) : std::nullopt;
        return std::nullopt;
    }

    std::string KeyboardSymbol(const std::string_view display)
    {
        static constexpr std::array<std::pair<std::string_view, std::string_view>, 64> aliases{{
            { "Esc", "ESCAPE" }, { "Ent", "RETURN" }, { "Bksp", "BACK" }, { "Space", "SPACE" },
            { "Tab", "TAB" }, { "Home", "HOME" }, { "End", "END" }, { "Up", "UP" },
            { "Down", "DOWN" }, { "Left", "LEFT" }, { "Right", "RIGHT" },
            { "PgUp", "PRIOR" }, { "PgDn", "NEXT" }, { "Ins", "INSERT" }, { "Del", "DELETE" },
            { "PrtSc", "SNAPSHOT" }, { "ScrLk", "SCROLLLOCK" }, { "NumLk", "NUMLOCK" },
            { "Caps", "CAPITAL" }, { "LCtrl", "LCONTROL" }, { "RCtrl", "RCONTROL" },
            { "LShift", "LSHIFT" }, { "RShift", "RSHIFT" }, { "LAlt", "LMENU" }, { "RAlt", "RMENU" },
            { "LWin", "LWIN" }, { "RWin", "RWIN" }, { "Num*", "MULTIPLY" }, { "Num+", "ADD" },
            { "Num-", "SUBTRACT" }, { "Num/", "DIVIDE" }, { "Num.", "DECIMAL" },
            { "Num0", "NUMPAD0" }, { "Num1", "NUMPAD1" }, { "Num2", "NUMPAD2" },
            { "Num3", "NUMPAD3" }, { "Num4", "NUMPAD4" }, { "Num5", "NUMPAD5" },
            { "Num6", "NUMPAD6" }, { "Num7", "NUMPAD7" }, { "Num8", "NUMPAD8" },
            { "Num9", "NUMPAD9" }, { "NumEnt", "NUMPADENTER" }, { "Num=", "NUMPADEQUALS" },
            { "Pause", "PAUSE" }, { "Menu", "APPS" }, { "Mute", "VOLUME_MUTE" },
            { "VolDown", "VOLUME_DOWN" }, { "VolUp", "VOLUME_UP" },
            { "PrevTrack", "MEDIA_PREV_TRACK" }, { "NextTrack", "MEDIA_NEXT_TRACK" },
            { "PlayPause", "MEDIA_PLAY_PAUSE" }, { "MediaStop", "MEDIA_STOP" },
            { "WebHome", "BROWSER_HOME" }, { "WebSearch", "BROWSER_SEARCH" },
            { "WebFavorites", "BROWSER_FAVORITES" }, { "WebRefresh", "BROWSER_REFRESH" },
            { "WebStop", "BROWSER_STOP" }, { "WebForward", "BROWSER_FORWARD" },
            { "WebBack", "BROWSER_BACK" }, { "MyComputer", "LAUNCH_APP1" },
            { "Mail", "LAUNCH_MAIL" }, { "MediaSelect", "LAUNCH_MEDIA_SELECT" },
            { "`", "OEM_3" }
        }};
        for (const auto& [shown, symbol] : aliases) if (display == shown) return std::string(symbol);
        if (display.size() == 1U || (display.starts_with('F') && display.size() <= 3U)) return UpperAscii(std::string(display));
        return {};
    }

    std::string GamepadSymbol(const std::uint32_t code)
    {
        static constexpr std::array<std::string_view, 16> names{
            "DPAD_UP", "DPAD_DOWN", "DPAD_LEFT", "DPAD_RIGHT", "START", "BACK", "L3", "R3",
            "LB", "RB", "A", "B", "X", "Y", "LT", "RT"
        };
        return code >= 266U && code < 282U ? std::string(names[code - 266U]) : std::string{};
    }

    std::string SymbolLike(const UHI::HotkeyRecord& record, const std::string_view device,
        const std::uint32_t code, const std::string_view display)
    {
        const auto rawUpper = UpperAscii(record.rawBinding);
        if (device == "keyboard") {
            auto symbol = KeyboardSymbol(display);
            if (symbol.empty()) return {};
            if (rawUpper.find("DIK_") != std::string::npos) return "DIK_" + symbol;
            if (rawUpper.find("VK_") != std::string::npos) return "VK_" + symbol;
            return std::string(display);
        }
        if (device == "mouse" && code < 5U) {
            static constexpr std::array<std::string_view, 5> mouseNumber{ "MOUSE1", "MOUSE2", "MOUSE3", "MOUSE4", "MOUSE5" };
            static constexpr std::array<std::string_view, 5> compact{ "LMB", "RMB", "MMB", "M4", "M5" };
            if (rawUpper.find("MOUSE") != std::string::npos || rawUpper.find("XBUTTON") != std::string::npos)
                return std::string(mouseNumber[code]);
            return std::string(compact[code]);
        }
        if (device == "gamepad") {
            auto symbol = GamepadSymbol(code);
            if (symbol.empty()) return {};
            if (rawUpper.find("GAMEPAD_") != std::string::npos) return "GAMEPAD_" + symbol;
            if (rawUpper.find("XINPUT_") != std::string::npos) return "XINPUT_" + symbol;
            return symbol;
        }
        return {};
    }
}

namespace UHI
{
    SerializedBinding SerializeCapturedBinding(const HotkeyRecord& record,
        const std::string_view mainDevice, const std::uint32_t mainCode,
        const std::string_view modifierDevice, const std::uint32_t modifierCode)
    {
        const auto mainDisplay = CapturedDisplay(mainDevice, mainCode);
        const auto modifierDisplay = modifierCode == 0U ? std::string{} : CapturedDisplay(modifierDevice, modifierCode);
        if (mainDisplay.empty() || (modifierCode != 0U && modifierDisplay.empty()))
            return { {}, {}, "This input code is not recognized." };

        const auto system = LowerAscii(record.codeSystem);
        std::string display = modifierDisplay.empty() ? mainDisplay : modifierDisplay + '+' + mainDisplay;
        if (system.find("symbol") != std::string::npos) {
            const auto mainRaw = SymbolLike(record, mainDevice, mainCode, mainDisplay);
            auto modifierRaw = modifierCode == 0U ? std::string{} :
                SymbolLike(record, modifierDevice, modifierCode, modifierDisplay);
            const auto originalUpper = UpperAscii(record.rawBinding);
            if (modifierDevice == "keyboard") {
                if ((modifierDisplay == "LCtrl" || modifierDisplay == "RCtrl") &&
                    originalUpper.find("CTRL") != std::string::npos) modifierRaw = "Ctrl";
                else if ((modifierDisplay == "LShift" || modifierDisplay == "RShift") &&
                    originalUpper.find("SHIFT") != std::string::npos) modifierRaw = "Shift";
                else if ((modifierDisplay == "LAlt" || modifierDisplay == "RAlt") &&
                    originalUpper.find("ALT") != std::string::npos) modifierRaw = "Alt";
            }
            if (mainRaw.empty() || (modifierCode != 0U && modifierRaw.empty()))
                return { {}, {}, "This symbolic setting cannot represent the captured input safely." };
            return { display, modifierRaw.empty() ? mainRaw : modifierRaw + '+' + mainRaw, {} };
        }

        const auto mainValue = CapturedCodeForSystem(record, mainDevice, mainCode);
        const auto modifierValue = modifierCode == 0U ? std::optional<std::uint32_t>{} :
            CapturedCodeForSystem(record, modifierDevice, modifierCode);
        if (!mainValue || (modifierCode != 0U && !modifierValue))
            return { {}, {}, "This device cannot be represented safely by the original setting format." };

        // Windows virtual-key storage cannot distinguish every physical
        // DirectInput key (notably main Enter from Numpad Enter). Refuse a
        // lossy write instead of displaying one key while persisting another.
        if ((system.find("windows") != std::string::npos || system.find("enb") != std::string::npos) &&
            system.find("community shaders") == std::string::npos) {
            const auto parsedMain = ParseVirtualKeyCode(*mainValue);
            if (!parsedMain.conflictEligible || parsedMain.binding != mainDisplay)
                return { {}, {}, "The original Windows key format cannot distinguish this physical key safely." };
        }

        if (system.find("reshade tuple") != std::string::npos) {
            unsigned ctrl{}, shift{}, alt{};
            if (modifierCode == 0x1DU || modifierCode == 0x9DU) ctrl = 1;
            else if (modifierCode == 0x2AU || modifierCode == 0x36U) shift = 1;
            else if (modifierCode == 0x38U || modifierCode == 0xB8U) alt = 1;
            else if (modifierCode != 0U)
                return { {}, {}, "ReShade supports Ctrl, Shift or Alt as the modifier." };
            if (modifierCode != 0U) {
                display = ctrl != 0U ? "Ctrl+" + mainDisplay :
                    shift != 0U ? "Shift+" + mainDisplay : "Alt+" + mainDisplay;
            }
            return { display, std::to_string(*mainValue) + ',' + std::to_string(ctrl) + ',' +
                std::to_string(shift) + ',' + std::to_string(alt), {} };
        }
        const auto mainRaw = NumericLike(*mainValue, record.rawBinding);
        if (system.find("enb") != std::string::npos) {
            // ENB stores one shared KeyCombination on a separate line.  A
            // per-action edit must therefore preserve that existing prefix;
            // silently accepting a different modifier would change what the
            // UI displays without changing the shared ENB setting.
            std::string existingModifier;
            if (const auto plus = record.binding.find('+'); plus != std::string::npos)
                existingModifier = record.binding.substr(0, plus);
            const auto modifierFamily = [&]() -> std::string {
                if (modifierCode == 0U) return {};
                if (modifierDisplay == "LCtrl" || modifierDisplay == "RCtrl") return "Ctrl";
                if (modifierDisplay == "LShift" || modifierDisplay == "RShift") return "Shift";
                if (modifierDisplay == "LAlt" || modifierDisplay == "RAlt") return "Alt";
                return modifierDisplay;
            }();
            if (modifierCode != 0U && (existingModifier.empty() || modifierFamily != existingModifier))
                return { {}, {}, "ENB uses a separate shared combination key; this action can only preserve its current modifier." };
            if (!existingModifier.empty()) display = existingModifier + '+' + mainDisplay;
            return { display, mainRaw, {} };
        }
        if (modifierCode == 0U) {
            if (system.find("community shaders") != std::string::npos && record.rawBinding.starts_with('['))
                return { display, '[' + mainRaw + ']', {} };
            return { display, mainRaw, {} };
        }
        // A conventional SKSE/MCM key-map option is one signed integer.  It
        // cannot persist a modifier and a main key in the same value.  Never
        // emit a visually plausible "42+63" string that the owning Papyrus
        // script would later fail to read as an integer.
        if (system.find("skse unified") != std::string::npos &&
            !record.rawBinding.starts_with('[') &&
            record.rawBinding.find(',') == std::string::npos &&
            record.rawBinding.find('+') == std::string::npos) {
            return { {}, {}, "This MCM/SKSE setting stores one key code and cannot represent a modifier chord." };
        }
        const auto modifierRaw = NumericLike(*modifierValue, record.rawBinding);
        if (system.find("community shaders") != std::string::npos || record.rawBinding.starts_with('['))
            return { display, '[' + modifierRaw + ',' + mainRaw + ']', {} };
        return { display, modifierRaw + '+' + mainRaw, {} };
    }
}
