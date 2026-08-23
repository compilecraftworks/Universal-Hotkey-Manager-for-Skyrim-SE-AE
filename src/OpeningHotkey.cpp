#include "UHI/OpeningHotkey.h"

#include "UHI/InputCodeFormatter.h"

#include <algorithm>
#include <charconv>
#include <cctype>
#include <fstream>
#include <iomanip>
#include <optional>
#include <sstream>
#include <string_view>
#include <vector>
#ifdef _WIN32
#include <Windows.h>
#endif

namespace
{
    bool ValidModifierCode(const bool enabled, const std::uint32_t code,
        const std::uint32_t left, const std::uint32_t right) noexcept
    {
        return enabled ? (code == 0 || code == left || code == right) : code == 0;
    }

    const char* ModifierName(const std::uint32_t code, const std::uint32_t left,
        const std::uint32_t right, const char* generic, const char* leftName, const char* rightName) noexcept
    {
        if (code == left) return leftName;
        if (code == right) return rightName;
        return generic;
    }

    std::string Trim(std::string value)
    {
        const auto space = [](const unsigned char character) { return std::isspace(character) != 0; };
        value.erase(value.begin(), std::find_if_not(value.begin(), value.end(), space));
        value.erase(std::find_if_not(value.rbegin(), value.rend(), space).base(), value.end());
        return value;
    }

    std::string Lower(std::string value)
    {
        std::ranges::transform(value, value.begin(), [](const unsigned char character) {
            return static_cast<char>(std::tolower(character));
        });
        return value;
    }

    std::optional<std::uint32_t> Number(std::string value)
    {
        value = Trim(std::move(value));
        const bool hexadecimal = value.size() > 2 && value[0] == '0' &&
            (value[1] == 'x' || value[1] == 'X');
        const auto* begin = value.data() + (hexadecimal ? 2 : 0);
        std::uint32_t result{};
        const auto [end, error] = std::from_chars(begin, value.data() + value.size(), result,
            hexadecimal ? 16 : 10);
        if (begin == value.data() + value.size() || error != std::errc{} ||
            end != value.data() + value.size()) return std::nullopt;
        return result;
    }

    std::optional<bool> Boolean(std::string value)
    {
        value = Lower(Trim(std::move(value)));
        if (value == "true" || value == "yes" || value == "on" || value == "1") return true;
        if (value == "false" || value == "no" || value == "off" || value == "0") return false;
        return std::nullopt;
    }

    std::optional<float> Decimal(std::string value)
    {
        value = Trim(std::move(value));
        float result{};
        const auto [end, error] = std::from_chars(value.data(), value.data() + value.size(), result);
        if (value.empty() || error != std::errc{} || end != value.data() + value.size()) return std::nullopt;
        return result;
    }

    UHI::UiLanguage Language(std::string value) noexcept
    {
        value = Lower(Trim(std::move(value)));
        if (value == "english" || value == "en") return UHI::UiLanguage::english;
        if (value == "korean" || value == "ko") return UHI::UiLanguage::korean;
        if (value == "chinese" || value == "zh") return UHI::UiLanguage::chinese;
        return UHI::UiLanguage::automatic;
    }

    const char* LanguageName(const UHI::UiLanguage language) noexcept
    {
        switch (language) {
        case UHI::UiLanguage::english: return "English";
        case UHI::UiLanguage::korean: return "Korean";
        case UHI::UiLanguage::chinese: return "Chinese";
        default: return "Auto";
        }
    }

#ifdef _WIN32
    std::optional<UHI::UiLanguage> LanguageFromId(const LANGID language) noexcept
    {
        switch (PRIMARYLANGID(language)) {
        case LANG_KOREAN: return UHI::UiLanguage::korean;
        case LANG_CHINESE: return UHI::UiLanguage::chinese;
        case LANG_ENGLISH: return UHI::UiLanguage::english;
        default: return std::nullopt;
        }
    }

    std::optional<UHI::UiLanguage> LanguageFromLocaleName(const wchar_t* locale) noexcept
    {
        if (!locale || !*locale) return std::nullopt;
        if (_wcsnicmp(locale, L"ko", 2) == 0) return UHI::UiLanguage::korean;
        if (_wcsnicmp(locale, L"zh", 2) == 0) return UHI::UiLanguage::chinese;
        if (_wcsnicmp(locale, L"en", 2) == 0) return UHI::UiLanguage::english;
        return std::nullopt;
    }
#endif

    std::string ComparableIniLine(std::string line)
    {
        if (line.starts_with("\xEF\xBB\xBF")) line.erase(0, 3);
        return Lower(Trim(std::move(line)));
    }
}

namespace UHI
{
    bool IsValidOpeningHotkey(const OpeningHotkey& hotkey) noexcept
    {
        // A modifier cannot be the base key: it would make exact modifier
        // matching ambiguous when the same event updates its held state.
        if (hotkey.scanCode == 0x1D || hotkey.scanCode == 0x9D ||
            hotkey.scanCode == 0x2A || hotkey.scanCode == 0x36 ||
            hotkey.scanCode == 0x38 || hotkey.scanCode == 0xB8) return false;
        return !FormatDirectInputScanCode(hotkey.scanCode).empty() &&
            ValidModifierCode(hotkey.ctrl, hotkey.ctrlScanCode, 0x1D, 0x9D) &&
            ValidModifierCode(hotkey.shift, hotkey.shiftScanCode, 0x2A, 0x36) &&
            ValidModifierCode(hotkey.alt, hotkey.altScanCode, 0x38, 0xB8);
    }

    std::string FormatOpeningHotkey(const OpeningHotkey& hotkey)
    {
        if (!IsValidOpeningHotkey(hotkey)) return "Invalid";
        std::string result;
        if (hotkey.ctrl) {
            result += ModifierName(hotkey.ctrlScanCode, 0x1D, 0x9D, "Ctrl", "LCtrl", "RCtrl");
            result += '+';
        }
        if (hotkey.shift) {
            result += ModifierName(hotkey.shiftScanCode, 0x2A, 0x36, "Shift", "LShift", "RShift");
            result += '+';
        }
        if (hotkey.alt) {
            result += ModifierName(hotkey.altScanCode, 0x38, 0xB8, "Alt", "LAlt", "RAlt");
            result += '+';
        }
        result += FormatDirectInputScanCode(hotkey.scanCode);
        return result;
    }

    UiLanguage ResolveUiLanguage(const UiLanguage configured) noexcept
    {
        if (configured != UiLanguage::automatic) return configured;
#ifdef _WIN32
        // Prefer a non-English Windows display language. Some installations
        // retain an English UI LANGID while the current user's locale is
        // Korean or Chinese, so consult the locale before accepting English.
        const auto uiLanguage = LanguageFromId(GetUserDefaultUILanguage());
        if (uiLanguage && *uiLanguage != UiLanguage::english) return *uiLanguage;
        wchar_t localeName[LOCALE_NAME_MAX_LENGTH]{};
        if (GetUserDefaultLocaleName(localeName, LOCALE_NAME_MAX_LENGTH) > 0) {
            if (const auto localeLanguage = LanguageFromLocaleName(localeName)) {
                return *localeLanguage;
            }
        }
        if (uiLanguage) return *uiLanguage;
        if (const auto systemLanguage = LanguageFromId(GetSystemDefaultUILanguage())) {
            return *systemLanguage;
        }
#endif
        return UiLanguage::english;
    }

    MenuFrameworkGlyphRangeStatus EnsureMenuFrameworkGlyphRange(
        const std::filesystem::path& path, const UiLanguage language) noexcept
    {
        const auto effective = ResolveUiLanguage(language);
        const char* requiredKey = effective == UiLanguage::korean ? "EnableKorean" :
            effective == UiLanguage::chinese ? "EnableChinese" : nullptr;
        if (!requiredKey) return MenuFrameworkGlyphRangeStatus::notRequired;

        try {
            std::ifstream input(path, std::ios::binary);
            if (!input) return MenuFrameworkGlyphRangeStatus::configMissing;
            std::string contents((std::istreambuf_iterator<char>(input)),
                std::istreambuf_iterator<char>());
            if (!input.eof() && input.fail()) return MenuFrameworkGlyphRangeStatus::writeFailed;
            // Windows does not allow MoveFileExW to replace the file while
            // this stream still owns a non-delete-sharing handle.
            input.close();

            const std::string newline = contents.find("\r\n") != std::string::npos ? "\r\n" : "\n";
            const bool endedWithNewline = !contents.empty() && contents.back() == '\n';
            std::vector<std::string> lines;
            std::istringstream stream(contents);
            for (std::string line; std::getline(stream, line);) {
                if (!line.empty() && line.back() == '\r') line.pop_back();
                lines.push_back(std::move(line));
            }
            if (contents.empty()) lines.clear();

            const auto requiredLower = Lower(requiredKey);
            bool inFonts = false;
            bool fontsFound = false;
            std::optional<std::size_t> insertAt;
            bool changed = false;
            for (std::size_t index = 0; index < lines.size(); ++index) {
                const auto comparable = ComparableIniLine(lines[index]);
                if (comparable.size() >= 2 && comparable.front() == '[' && comparable.back() == ']') {
                    if (inFonts && !insertAt) insertAt = index;
                    inFonts = comparable == "[fonts]";
                    fontsFound = fontsFound || inFonts;
                    continue;
                }
                if (!inFonts) continue;
                const auto separator = comparable.find('=');
                if (separator == std::string::npos ||
                    Trim(comparable.substr(0, separator)) != requiredLower) continue;
                auto valueText = comparable.substr(separator + 1);
                if (const auto comment = valueText.find_first_of(";#");
                    comment != std::string::npos) valueText.erase(comment);
                const auto value = Lower(Trim(std::move(valueText)));
                if (value == "true" || value == "1" || value == "yes" || value == "on") {
                    return MenuFrameworkGlyphRangeStatus::alreadyEnabled;
                }

                // Replace only the setting value and preserve indentation plus
                // any trailing comment from the original framework INI.
                const auto originalSeparator = lines[index].find('=');
                if (originalSeparator == std::string::npos) continue;
                const auto comment = lines[index].find_first_of(";#", originalSeparator + 1);
                std::string suffix;
                if (comment != std::string::npos) suffix = " " + Trim(lines[index].substr(comment));
                lines[index] = lines[index].substr(0, originalSeparator + 1) + " true" + suffix;
                changed = true;
                break;
            }

            if (!changed) {
                if (!fontsFound) {
                    if (!lines.empty() && !lines.back().empty()) lines.emplace_back();
                    lines.emplace_back("[Fonts]");
                    lines.emplace_back(std::string(requiredKey) + " = true");
                } else {
                    const auto position = insertAt.value_or(lines.size());
                    lines.insert(lines.begin() + static_cast<std::ptrdiff_t>(position),
                        std::string(requiredKey) + " = true");
                }
            }

            auto temporary = path;
            temporary += ".uhi.tmp";
            std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
            if (!output) return MenuFrameworkGlyphRangeStatus::writeFailed;
            for (std::size_t index = 0; index < lines.size(); ++index) {
                output << lines[index];
                if (index + 1 < lines.size() || endedWithNewline) output << newline;
            }
            output.close();
            if (!output) {
                std::error_code error;
                std::filesystem::remove(temporary, error);
                return MenuFrameworkGlyphRangeStatus::writeFailed;
            }
#ifdef _WIN32
            if (!MoveFileExW(temporary.c_str(), path.c_str(),
                    MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
                std::error_code error;
                std::filesystem::remove(temporary, error);
                return MenuFrameworkGlyphRangeStatus::writeFailed;
            }
#else
            std::error_code error;
            std::filesystem::rename(temporary, path, error);
            if (error) {
                std::filesystem::remove(temporary, error);
                return MenuFrameworkGlyphRangeStatus::writeFailed;
            }
#endif
            return MenuFrameworkGlyphRangeStatus::updated;
        } catch (...) {
            return MenuFrameworkGlyphRangeStatus::writeFailed;
        }
    }

    OpeningHotkey LoadOpeningHotkey(const std::filesystem::path& path) noexcept
    {
        OpeningHotkey result;
        try {
            std::ifstream input(path);
            if (!input) return result;
            bool inGeneral = false;
            for (std::string line; std::getline(input, line);) {
                line = Trim(std::move(line));
                if (line.empty() || line.starts_with(';') || line.starts_with('#')) continue;
                if (line.front() == '[' && line.back() == ']') {
                    inGeneral = Lower(line.substr(1, line.size() - 2)) == "general";
                    continue;
                }
                if (!inGeneral) continue;
                const auto separator = line.find('=');
                if (separator == std::string::npos) continue;
                const auto name = Lower(Trim(line.substr(0, separator)));
                const auto value = Trim(line.substr(separator + 1));
                if (name == "togglekey") {
                    if (const auto parsed = Number(value)) result.scanCode = *parsed;
                } else if (name == "ctrl") {
                    if (const auto parsed = Boolean(value)) result.ctrl = *parsed;
                } else if (name == "shift") {
                    if (const auto parsed = Boolean(value)) result.shift = *parsed;
                } else if (name == "alt") {
                    if (const auto parsed = Boolean(value)) result.alt = *parsed;
                } else if (name == "ctrlkey") {
                    if (const auto parsed = Number(value)) result.ctrlScanCode = *parsed;
                } else if (name == "shiftkey") {
                    if (const auto parsed = Number(value)) result.shiftScanCode = *parsed;
                } else if (name == "altkey") {
                    if (const auto parsed = Number(value)) result.altScanCode = *parsed;
                } else if (name == "uiscale") {
                    if (const auto parsed = Decimal(value)) result.uiScale = std::clamp(*parsed, 0.80F, 1.35F);
                } else if (name == "windowopacity") {
                    if (const auto parsed = Decimal(value))
                        result.windowOpacity = std::clamp(*parsed, 0.35F, 1.0F);
                } else if (name == "language") {
                    result.uiLanguage = Language(value);
                }
            }
            return IsValidOpeningHotkey(result) ? result : OpeningHotkey{};
        } catch (...) {
            return OpeningHotkey{};
        }
    }

    bool SaveOpeningHotkey(const std::filesystem::path& path, const OpeningHotkey& hotkey) noexcept
    {
        if (!IsValidOpeningHotkey(hotkey)) return false;
        try {
            std::error_code error;
            std::filesystem::create_directories(path.parent_path(), error);
            if (error) return false;
            auto temporary = path;
            temporary += ".tmp";
            std::ofstream output(temporary, std::ios::trunc);
            if (!output) return false;
            output << "; Universal Hotkey Manager for Skyrim SE-AE native settings\n"
                   << "; ToggleKey is a DirectInput keyboard scan code. 0xD3 = Delete.\n"
                   << "; Modifier flags match either the left or right key.\n"
                   << "[General]\nToggleKey=0x" << std::hex << std::uppercase << hotkey.scanCode << std::dec
                   << "\nCtrl=" << (hotkey.ctrl ? "true" : "false")
                   << "\nShift=" << (hotkey.shift ? "true" : "false")
                   << "\nAlt=" << (hotkey.alt ? "true" : "false")
                   << "\nCtrlKey=0x" << std::hex << hotkey.ctrlScanCode
                   << "\nShiftKey=0x" << hotkey.shiftScanCode
                   << "\nAltKey=0x" << hotkey.altScanCode << std::dec
                   << "\nUiScale=" << std::fixed << std::setprecision(2)
                   << std::clamp(hotkey.uiScale, 0.80F, 1.35F)
                   << "\nWindowOpacity=" << std::fixed << std::setprecision(2)
                   << std::clamp(hotkey.windowOpacity, 0.35F, 1.0F)
                   << "\nLanguage=" << LanguageName(hotkey.uiLanguage) << '\n';
            output.close();
            if (!output) { std::filesystem::remove(temporary, error); return false; }
#ifdef _WIN32
            if (!MoveFileExW(temporary.c_str(), path.c_str(),
                    MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
                std::filesystem::remove(temporary, error);
                return false;
            }
#else
            std::filesystem::rename(temporary, path, error);
            if (error) { std::filesystem::remove(temporary, error); return false; }
#endif
            return true;
        } catch (...) {
            return false;
        }
    }
}
