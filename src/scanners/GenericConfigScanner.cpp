#include "UHI/scanners/GenericConfigScanner.h"
#include "UHI/ConfigBindingParser.h"
#include "UHI/TextDecoder.h"
#include "UHI/GameFilePolicy.h"
#include "UHI/ScanWorker.h"
#include "UHI/ActivationContextInference.h"
#include "UHI/PathEncoding.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <fstream>
#include <regex>
#include <atomic>
#include <mutex>
#include <optional>
#include <thread>
#include <tuple>
#include <unordered_map>
#include <unordered_set>

namespace
{
    std::string Lower(std::string value)
    {
        std::ranges::transform(value, value.begin(), [](const unsigned char character) {
            return static_cast<char>(std::tolower(character));
        });
        return value;
    }

    std::string Trim(std::string value)
    {
        const auto notSpace = [](const unsigned char character) { return !std::isspace(character); };
        const auto first = std::ranges::find_if(value, notSpace);
        if (first == value.end()) return {};
        const auto last = std::find_if(value.rbegin(), value.rend(), notSpace).base();
        return std::string(first, last);
    }

    bool IsConfig(const std::filesystem::path& path)
    {
        const auto extension = Lower(UHI::PathToUtf8(path.extension()));
        if (extension == ".ini" || extension == ".json" || extension == ".jsonc" || extension == ".toml" ||
            extension == ".yaml" || extension == ".yml" || extension == ".xml" || extension == ".cfg" ||
            extension == ".conf" || extension == ".settings" || extension == ".properties") {
            return true;
        }
        if (extension != ".txt") return false;
        const auto lowered = Lower(UHI::PathToUtf8(path));
        return lowered.find("hotkey") != std::string::npos || lowered.find("keybind") != std::string::npos ||
            lowered.find("input") != std::string::npos || lowered.find("control") != std::string::npos ||
            lowered.find("config") != std::string::npos || lowered.find("settings") != std::string::npos ||
            lowered.find("/skse/plugins/") != std::string::npos || lowered.find("/mcm/") != std::string::npos;
    }

    bool ContainsAsciiInsensitive(const std::string_view bytes, const std::string_view needle)
    {
        if (needle.empty() || bytes.size() < needle.size()) return false;
        const auto equal = [](const char left, const char right) {
            return std::tolower(static_cast<unsigned char>(left)) ==
                std::tolower(static_cast<unsigned char>(right));
        };
        if (std::search(bytes.begin(), bytes.end(), needle.begin(), needle.end(), equal) != bytes.end()) return true;
        if (bytes.find('\0') == std::string_view::npos || bytes.size() < needle.size() * 2U) return false;
        const auto matchesUtf16 = [&](const std::size_t position, const bool littleEndian) {
            for (std::size_t index = 0; index < needle.size(); ++index) {
                const auto characterOffset = position + index * 2U + (littleEndian ? 0U : 1U);
                const auto zeroOffset = position + index * 2U + (littleEndian ? 1U : 0U);
                if (bytes[zeroOffset] != '\0' || !equal(bytes[characterOffset], needle[index])) return false;
            }
            return true;
        };
        for (std::size_t position = 0; position + needle.size() * 2U <= bytes.size(); ++position) {
            if (matchesUtf16(position, true) || matchesUtf16(position, false)) return true;
        }
        return false;
    }

    bool LooksLikeBinding(const std::string& key)
    {
        const auto lowered = Lower(key);
        // "key" is also a common data noun (keyword records, lock/key item
        // weights and loot counts). Those are not physical input bindings.
        if (lowered.find("keyword") != std::string::npos ||
            lowered.find("keyweight") != std::string::npos ||
            lowered.find("key_weight") != std::string::npos ||
            lowered.find("keychance") != std::string::npos ||
            lowered.find("keycount") != std::string::npos ||
            lowered.find("keyamount") != std::string::npos ||
            ((lowered.starts_with("min") || lowered.starts_with("max")) &&
                lowered.find("keys") != std::string::npos)) {
            return false;
        }
        if (lowered.find("modifierkey") != std::string::npos ||
            lowered.find("modifierbutton") != std::string::npos || lowered == "combokey" ||
            lowered.find("enable") != std::string::npos || lowered.find("override") != std::string::npos ||
            lowered.starts_with("prefer") || lowered.starts_with("maxheld")) {
            return false;
        }
        const auto camelInputWord = [&](const std::string_view word) {
            std::size_t position{};
            while ((position = lowered.find(word, position)) != std::string::npos) {
                const bool boundary = position == 0 || !std::isalnum(static_cast<unsigned char>(key[position - 1])) ||
                    std::isupper(static_cast<unsigned char>(key[position])) != 0;
                const auto after = position + word.size();
                // Do not treat the "Key" prefix in Keyboard/Keyword as a
                // standalone input-setting token.
                if (word == "key" && after < lowered.size() &&
                    (lowered.compare(after, 5, "board") == 0 ||
                     lowered.compare(after, 4, "word") == 0)) {
                    position = after;
                    continue;
                }
                if (boundary) return true;
                position += word.size();
            }
            return false;
        };
        // A number of SkyUI/MCM mods use the old Hungarian-style `hk*`
        // prefix (Defeat's hkOption/hkAction/hkSurrender are common
        // examples).  It means hotkey even when the full word never appears
        // in the serialized JSON/INI setting name.
        const bool abbreviatedHotkey = lowered.size() > 2U && lowered.starts_with("hk") &&
            std::ranges::any_of(lowered.substr(2), [](const unsigned char character) {
                return std::isalnum(character) != 0;
            });
        return lowered == "key" || lowered == "ikey" || lowered == "keycode" || lowered == "scancode" ||
               lowered == "inputkey" || lowered == "hotkeycode" || lowered == "button" ||
               abbreviatedHotkey ||
               lowered.find("hotkey") != std::string::npos || lowered.find("keybind") != std::string::npos ||
               lowered.find("shortcut") != std::string::npos || lowered.find("keycode") != std::string::npos ||
               lowered.find("scancode") != std::string::npos || lowered.find("inputkey") != std::string::npos ||
               lowered.find("virtualkey") != std::string::npos || lowered.find("keymap") != std::string::npos ||
               lowered.find("inputmap") != std::string::npos || lowered.find("dik") != std::string::npos ||
               lowered.find("key_") != std::string::npos || lowered.ends_with("key") ||
               lowered.ends_with("keys") || camelInputWord("key") || camelInputWord("button") ||
               camelInputWord("bind") || lowered.find("keymkb") != std::string::npos ||
               lowered.find("keygamepad") != std::string::npos || lowered.find("togglebutton") != std::string::npos;
    }

    bool IsDisabled(std::string value)
    {
        value = Lower(std::move(value));
        return value.empty() || value == "0" || value == "false" || value == "none" || value == "null" ||
            value == "disabled" || value == "unbound" || value == "-1" || value == "255" || value == "0xff";
    }

    std::size_t LineAt(const std::string_view text, const std::size_t offset)
    {
        return 1 + static_cast<std::size_t>(std::count(text.begin(), text.begin() +
            static_cast<std::ptrdiff_t>(std::min(offset, text.size())), '\n'));
    }

    bool EnabledModifier(const std::string_view context, const std::string_view name)
    {
        const std::regex expression("[\\\"']?" + std::string(name) +
            R"([\w.-]*[\"']?\s*[:=]\s*[\"']?(?:true|yes|on|1))", std::regex::icase);
        return std::regex_search(context.begin(), context.end(), expression);
    }

    std::string ApplyModifiers(std::string binding, const std::string_view context,
        const std::filesystem::path& source, const std::string_view settingName)
    {
        if (binding.find('+') != std::string::npos || binding.empty()) return binding;
        std::string prefix;
        const auto filename = Lower(UHI::PathToUtf8(source.filename()));
        const auto loweredSetting = Lower(std::string(settingName));
        if (filename == "ssedisplaytweaks.ini" && loweredSetting == "togglekey") {
            static const std::regex combo(R"(\bComboKey\s*=\s*([1-8])\b)", std::regex::icase);
            std::match_results<std::string_view::const_iterator> match;
            if (std::regex_search(context.begin(), context.end(), match, combo)) {
                static constexpr std::array<std::string_view, 8> modifiers{
                    "LShift+", "RShift+", "LCtrl+", "RCtrl+",
                    "LAlt+", "RAlt+", "LWin+", "RWin+"
                };
                prefix += modifiers[static_cast<std::size_t>(match[1].str()[0] - '1')];
            }
        } else if (filename == "improvedcamerase.ini" && loweredSetting == "menukey") {
            // Improved Camera documents this modifier as fixed behavior rather
            // than a separate setting: hold Left Shift, then press MenuKey.
            prefix += "LShift+";
        }
        const auto serializedModifier = [&](const std::string_view property) {
            const std::regex expression("\\b" + std::string(property) + R"(\s*=\s*(0[xX][0-9A-Fa-f]+|[0-9]+)\b)",
                std::regex::icase);
            std::match_results<std::string_view::const_iterator> match;
            if (!std::regex_search(context.begin(), context.end(), match, expression) ||
                match[1].str() == "0") return std::string{};
            const auto parsed = UHI::ParseConfigBinding(std::string(property), match[1].str(),
                UHI::NumericCodeSpace::skseUnifiedInputCode);
            return parsed.conflictEligible ? parsed.binding + '+' : std::string{};
        };
        if (loweredSetting.find("keymkb") != std::string::npos) {
            prefix += serializedModifier("ModifierKeyMKB");
        } else if (loweredSetting.find("keygamepad") != std::string::npos) {
            prefix += serializedModifier("ModifierButtonGamepad");
        }
        if (EnabledModifier(context, "(?:ctrl|control)")) prefix += "Ctrl+";
        if (EnabledModifier(context, "shift")) prefix += "Shift+";
        if (EnabledModifier(context, "alt")) prefix += "Alt+";
        return prefix + binding;
    }

    std::string NearbyAction(const std::string_view context)
    {
        static const std::regex action(
            R"(["']?(?:action|command|label|description)["']?\s*[:=]\s*["']([^"']{1,96})["'])",
            std::regex::icase);
        std::match_results<std::string_view::const_iterator> match;
        return std::regex_search(context.begin(), context.end(), match, action) ? match[1].str() : std::string{};
    }

    bool IsGenericActionSetting(std::string name)
    {
        name = Lower(std::move(name));
        std::erase_if(name, [](const unsigned char character) {
            return !std::isalnum(character);
        });
        static const std::unordered_set<std::string> generic{
            "hotkey", "key", "ikey", "button", "keycode", "scancode", "inputkey",
            "hotkeycode", "virtualkey", "keymap", "shortcut", "keybind", "keycustom",
            "keydefine", "primarykey", "primarykeydefine", "togglekey", "keyboardkey",
            "gamepadkey", "mousekey", "buttonmap", "keys", "gamepadkeys", "keyboardkeys"
        };
        return generic.contains(name);
    }

    std::string HumanizeActionSetting(std::string name);

    bool MeaningfulAction(std::string value)
    {
        value = Trim(std::move(value));
        if (value.size() <= 1U) return false;
        const auto lowered = Lower(value);
        if (lowered.find("http://") != std::string::npos || lowered.find("https://") != std::string::npos ||
            lowered.find("creationkit.com") != std::string::npos || lowered.find("uesp.net") != std::string::npos ||
            lowered.find("geckwiki.com") != std::string::npos || lowered.find("dxscan") != std::string::npos ||
            lowered.starts_with("keycode=") || lowered.starts_with("gamepad scan code")) return false;
        std::erase_if(value, [](const unsigned char character) { return !std::isalnum(character); });
        const auto compact = Lower(value);
        static const std::unordered_set<std::string> generic{
            "key", "keys", "keybind", "keybinds", "keycode", "scancode", "input",
            "keyboard", "gamepad", "mouse", "button"
        };
        return !compact.empty() && !generic.contains(compact);
    }

    std::string NearbyJsonObjectAction(const std::string_view content, const std::size_t offset)
    {
        const auto bounded = (std::min)(offset, content.size());
        const auto start = bounded > 1024U ? bounded - 1024U : 0U;
        const auto prefix = content.substr(start, bounded - start);
        static const std::regex object(R"REGEX("([^"]{1,96})"\s*:\s*\{)REGEX");
        std::string result;
        using Iterator = std::string_view::const_iterator;
        for (std::regex_iterator<Iterator> iterator(prefix.begin(), prefix.end(), object), end;
             iterator != end; ++iterator) result = (*iterator)[1].str();
        if (result.empty()) return {};
        const auto lowered = Lower(result);
        if (lowered == "idlestopw") return "Cancel Bow Combo - Forward";
        if (lowered == "idlestopa") return "Cancel Bow Combo - Left";
        if (lowered == "idlestops") return "Cancel Bow Combo - Back";
        if (lowered == "idlestopd") return "Cancel Bow Combo - Right";
        if (lowered.starts_with("idlestop")) return "Cancel Bow Combo";
        if (lowered.starts_with("powershot")) return "Power Shot";
        return HumanizeActionSetting(std::move(result));
    }

    std::string NearbySection(const std::string_view content, const std::size_t offset)
    {
        if (content.empty()) return {};
        const auto boundedOffset = (std::min)(offset, content.size());
        const auto start = boundedOffset > 2048U ? boundedOffset - 2048U : 0U;
        const auto prefix = content.substr(start, boundedOffset - start);
        static const std::regex section(R"((?:^|[\r\n])\s*\[([^\]\r\n]{1,96})\]\s*(?:[\r\n]|$))");
        std::string result;
        using Iterator = std::string_view::const_iterator;
        for (std::regex_iterator<Iterator> iterator(prefix.begin(), prefix.end(), section), end;
             iterator != end; ++iterator) {
            result = (*iterator)[1].str();
        }
        return result;
    }

    bool IsBindingSection(const std::string_view section)
    {
        auto lowered = Lower(std::string(section));
        lowered = Trim(std::move(lowered));
        std::erase_if(lowered, [](const unsigned char character) {
            return character == '_' || character == '-' || character == '.' || std::isspace(character);
        });
        static const std::unordered_set<std::string> sections{
            "key", "keys", "hotkey", "hotkeys", "keybind", "keybinds",
            "binding", "bindings", "shortcut", "shortcuts", "input", "inputs",
            "keyboard", "mouse", "gamepad", "controller", "control", "controls"
        };
        return sections.contains(lowered);
    }

    bool BareBindingNameHasMeaning(const std::string& key, const std::string_view context,
        const std::string_view content, const std::size_t offset)
    {
        const auto lowered = Lower(key);
        if (lowered != "key" && lowered != "button") return true;
        if (!NearbyAction(context).empty()) return true;
        const auto section = Lower(NearbySection(content, offset));
        return section.find("hotkey") != std::string::npos || section.find("keybind") != std::string::npos ||
            section.find("input") != std::string::npos || section.find("control") != std::string::npos ||
            section.find("shortcut") != std::string::npos;
    }

    std::string HumanizeActionSetting(std::string name)
    {
        auto lowered = Lower(name);
        const auto removeSuffix = [&](const std::string_view suffix) {
            if (lowered.size() <= suffix.size() || !lowered.ends_with(suffix)) return false;
            name.resize(name.size() - suffix.size());
            lowered.resize(lowered.size() - suffix.size());
            return true;
        };
        const auto removePrefix = [&](const std::string_view prefix) {
            if (lowered.size() <= prefix.size() || !lowered.starts_with(prefix)) return false;
            name.erase(0, prefix.size());
            lowered.erase(0, prefix.size());
            return true;
        };

        // Device/serialization words describe how the binding is stored, not
        // what pressing it does.
        for (const auto suffix : { "keyboard", "gamepad", "mouse", "mkb" }) {
            if (removeSuffix(suffix)) break;
        }
        for (const auto suffix : { "hotkey", "shortcut", "keybind", "keycode", "scancode",
                 "virtualkey", "inputkey", "buttonmap", "keymap", "button", "keys", "key", "bind" }) {
            if (removeSuffix(suffix)) break;
        }
        for (const auto prefix : { "hotkey", "shortcut", "keybind", "keycode", "scancode",
                 "virtualkey", "inputkey", "buttonmap", "keymap", "button", "key" }) {
            if (removePrefix(prefix)) break;
        }
        // Papyrus and native settings commonly use Hungarian-style integer
        // prefixes (`iSelectorHotkey`, `iMenuKey`).  The leading `i` describes
        // the stored type; it is not part of the user-facing action name.
        while (name.size() > 1U && (name.starts_with("m_") || name.starts_with("M_"))) {
            name.erase(0, 2U);
        }
        if (name.size() > 1U && (name.front() == 'i' || name.front() == 'I') &&
            (std::isupper(static_cast<unsigned char>(name[1])) || name[1] == '_')) {
            name.erase(0, name[1] == '_' ? 2U : 1U);
        }
        lowered = Lower(name);
        for (const auto prefix : { "hotkey", "shortcut", "keybind", "keymap", "button", "key" }) {
            if (removePrefix(prefix)) break;
        }
        name = Trim(std::move(name));
        if (name.empty()) return {};

        std::string result;
        result.reserve(name.size() + 8U);
        for (std::size_t index = 0; index < name.size(); ++index) {
            const auto character = static_cast<unsigned char>(name[index]);
            if (character == '_' || character == '-' || character == '.') {
                if (!result.empty() && result.back() != ' ') result.push_back(' ');
                continue;
            }
            const bool wordBoundary = index > 0U && std::isupper(character) &&
                (std::islower(static_cast<unsigned char>(name[index - 1])) ||
                    std::isdigit(static_cast<unsigned char>(name[index - 1])));
            if (wordBoundary && !result.empty() && result.back() != ' ') result.push_back(' ');
            result.push_back(static_cast<char>(character));
        }
        while (!result.empty() && result.back() == ' ') result.pop_back();
        return result;
    }

    std::string NearbyCommentAction(const std::string_view content, const std::size_t offset)
    {
        if (content.empty()) return {};
        const auto bounded = (std::min)(offset, content.size());
        const auto currentStartFound = content.rfind('\n', bounded);
        const auto currentStart = currentStartFound == std::string_view::npos ? 0U : currentStartFound + 1U;
        const auto currentEndFound = content.find('\n', bounded);
        const auto currentEnd = currentEndFound == std::string_view::npos ? content.size() : currentEndFound;

        const auto clean = [](std::string candidate) {
            candidate = Trim(candidate);
            while (!candidate.empty() && (candidate.front() == '#' || candidate.front() == ';' ||
                candidate.front() == '/' || candidate.front() == '-' || candidate.front() == ' ')) {
                candidate.erase(candidate.begin());
            }
            candidate = Trim(candidate);
            static const std::regex defaultTail(
                R"((?:[.;,\s-]*(?:default|range|min(?:imum)?|max(?:imum)?)\s*[:=].*)$)",
                std::regex::icase);
            candidate = std::regex_replace(candidate, defaultTail, "");
            candidate = Trim(candidate);
            if (candidate.size() < 3U || candidate.size() > 120U ||
                !std::ranges::any_of(candidate, [](const unsigned char value) {
                    return value >= 0x80U || std::isalpha(value);
                })) {
                return std::string{};
            }
            const auto lowered = Lower(candidate);
            if (lowered.starts_with("default") || lowered.starts_with("0x") ||
                lowered == "keyboard" || lowered == "gamepad" || lowered == "mouse") return std::string{};
            return candidate;
        };

        const auto currentLine = content.substr(currentStart, currentEnd - currentStart);
        std::size_t marker = std::string_view::npos;
        for (const auto token : { std::string_view("#"), std::string_view(";"), std::string_view("//") }) {
            const auto found = currentLine.find(token);
            if (found != std::string_view::npos && (marker == std::string_view::npos || found < marker)) marker = found;
        }
        if (marker != std::string_view::npos) {
            if (auto result = clean(std::string(currentLine.substr(marker))); !result.empty()) return result;
        }

        if (currentStart == 0U) return {};
        const auto previousEnd = currentStart - 1U;
        const auto previousStartFound = content.rfind('\n', previousEnd == 0U ? 0U : previousEnd - 1U);
        const auto previousStart = previousStartFound == std::string_view::npos ? 0U : previousStartFound + 1U;
        const auto previousLine = Trim(std::string(content.substr(previousStart, previousEnd - previousStart)));
        if (previousLine.starts_with('#') || previousLine.starts_with(';') || previousLine.starts_with("//")) {
            return clean(previousLine);
        }
        return {};
    }

    bool IsVagueAction(const std::string& action)
    {
        const auto lowered = Lower(action);
        static const std::unordered_set<std::string> vague{
            "selector", "select", "selection", "option", "action", "command", "function", "input"
        };
        return vague.contains(lowered);
    }

    std::string ResolveActionName(const std::string& key, const std::string_view context,
        const std::string_view content, const std::size_t offset, const std::string_view owner)
    {
        if (IsGenericActionSetting(key)) {
            if (auto action = NearbyAction(context); MeaningfulAction(action)) return action;
            if (auto action = NearbyCommentAction(content, offset); MeaningfulAction(action)) return action;
            if (auto action = NearbyJsonObjectAction(content, offset); MeaningfulAction(action)) return action;
            if (auto section = NearbySection(content, offset); MeaningfulAction(section)) return section;
            // A bare `Hotkey=F7` carries no semantic information. The owning
            // mod/file is more honest and useful than repeating "Hotkey".
            return std::string(owner);
        }
        if (auto comment = NearbyCommentAction(content, offset); MeaningfulAction(comment)) return comment;
        if (auto action = HumanizeActionSetting(key); !action.empty()) {
            if (!MeaningfulAction(action)) {
                if (auto object = NearbyJsonObjectAction(content, offset); MeaningfulAction(object)) return object;
                return std::string(owner);
            }
            if (IsVagueAction(action)) {
                const auto section = HumanizeActionSetting(NearbySection(content, offset));
                if (!section.empty() && !IsVagueAction(section) &&
                    Lower(section) != "general" && Lower(section) != "settings" && Lower(section) != "input") {
                    return section + ' ' + action;
                }
                return std::string(owner) + ' ' + action;
            }
            return action;
        }
        return std::string(owner);
    }

    UHI::NumericCodeSpace InferNumericCodeSpace(std::string name)
    {
        name = Lower(std::move(name));
        if (name.find("virtualkey") != std::string::npos || name.find("virtual_key") != std::string::npos ||
            name.find("vkcode") != std::string::npos || name.starts_with("vk") || name.ends_with("vk")) {
            return UHI::NumericCodeSpace::windowsVirtualKey;
        }
        if (name.find("directinput") != std::string::npos || name.find("scancode") != std::string::npos ||
            name.find("scan_code") != std::string::npos || name.find("dik") != std::string::npos ||
            name.find("dxkey") != std::string::npos) {
            return UHI::NumericCodeSpace::directInputScanCode;
        }
        if (name.find("skse") != std::string::npos || name.find("keymap") != std::string::npos ||
            name.find("inputmap") != std::string::npos) {
            return UHI::NumericCodeSpace::skseUnifiedInputCode;
        }
        // Dynamic Key Action Framework stores its keyboard and gamepad
        // bindings as `Keys` / `GamepadKeys`. They are SKSE's unified input
        // numbers even though the property names do not contain KeyCode.
        if (name == "keys" || name == "keyboardkeys" || name == "gamepadkeys") {
            return UHI::NumericCodeSpace::skseUnifiedInputCode;
        }
        return UHI::NumericCodeSpace::unknown;
    }

    UHI::NumericCodeSpace InferContextCodeSpace(const std::string_view context)
    {
        auto lowered = Lower(std::string(context));
        if (lowered.find("virtual-key") != std::string::npos || lowered.find("virtual key") != std::string::npos ||
            lowered.find("win32 key") != std::string::npos || lowered.find("vk_code") != std::string::npos ||
            lowered.find("vk code") != std::string::npos) {
            return UHI::NumericCodeSpace::windowsVirtualKey;
        }
        if (lowered.find("directinput") != std::string::npos || lowered.find("dx scan") != std::string::npos ||
            lowered.find("scan code") != std::string::npos) {
            return UHI::NumericCodeSpace::directInputScanCode;
        }
        if (lowered.find("input_script") != std::string::npos || lowered.find("registerforkey") != std::string::npos) {
            return UHI::NumericCodeSpace::skseUnifiedInputCode;
        }
        return UHI::NumericCodeSpace::unknown;
    }

    UHI::NumericCodeSpace InferPathCodeSpace(const std::filesystem::path& source)
    {
        const auto path = Lower(UHI::PathToUtf8(source));
        const auto filename = Lower(UHI::PathToUtf8(source.filename()));
        if (filename == "improvedcamerase.ini") return UHI::NumericCodeSpace::windowsVirtualKey;
        if (filename == "ssedisplaytweaks.ini" || filename == "immersiveequipmentdisplays.ini") {
            return UHI::NumericCodeSpace::directInputScanCode;
        }
        if (path.find("/mcm/") != std::string::npos || path.find("/jcuser/") != std::string::npos ||
            path.find("/skse/plugins/") != std::string::npos || path.find(".bsa::") != std::string::npos) {
            return UHI::NumericCodeSpace::skseUnifiedInputCode;
        }
        return UHI::NumericCodeSpace::unknown;
    }

    bool HasSpecializedScanner(const std::filesystem::path& path)
    {
        if (UHI::IsLooseControlMap(path)) return true;
        const auto filename = Lower(UHI::PathToUtf8(path.filename()));
        const auto wholePath = Lower(UHI::PathToUtf8(path));
        if (filename == "universalhotkeyinspector.ini" || filename == "universalhotkeymanager.ini") return true;
        if (filename.starts_with("reshade") && path.extension() == ".ini") return true;
        if (filename == "enblocal.ini") return true;
        return wholePath.find("communityshaders") != std::string::npos &&
            (filename == "settingsuser.json" || filename == "settingsdefault.json");
    }

    bool IsNonRuntimeResource(const std::filesystem::path& path)
    {
        const auto filename = Lower(UHI::PathToUtf8(path.filename()));
        const auto wholePath = Lower(UHI::PathToUtf8(path));
        const bool documentationFile = filename == "meta.ini" || filename.ends_with("_kid.ini") ||
            filename == "manifest.json" ||
            filename == "package.json" || filename.starts_with("readme") || filename.starts_with("license") ||
            filename.starts_with("changelog") || filename.starts_with("change-log") ||
            filename.starts_with("credits");
        const bool inactiveVariant = filename.find("preset") != std::string::npos ||
            filename.find("profile") != std::string::npos || filename.find("template") != std::string::npos ||
            filename.find("example") != std::string::npos || filename.find("sample") != std::string::npos ||
            filename.find("_default.") != std::string::npos || filename.find("-default.") != std::string::npos ||
            filename.find(".default.") != std::string::npos || filename.find("_defaults.") != std::string::npos ||
            filename.find("-defaults.") != std::string::npos || filename.find(".defaults.") != std::string::npos;
        return documentationFile || inactiveVariant || filename == "creationkitprefs.ini" ||
            filename.starts_with("creationkit") || filename.starts_with("skyrim64_test") ||
            filename.find("translation") != std::string::npos ||
            filename.find("localization") != std::string::npos || filename.find("settingstheme") != std::string::npos ||
            filename.ends_with("_cache.json") ||
            wholePath.find("/translations/") != std::string::npos || wholePath.find("/language/") != std::string::npos ||
            wholePath.find("/lang/") != std::string::npos || wholePath.find("/savedsettings/") != std::string::npos ||
            wholePath.find("/localization/") != std::string::npos || wholePath.find("/themes/") != std::string::npos ||
            wholePath.find("/styles/") != std::string::npos || wholePath.find("/presets/") != std::string::npos ||
            wholePath.find("/backup/") != std::string::npos || wholePath.find("/backups/") != std::string::npos ||
            wholePath.find("/meshes/") != std::string::npos || wholePath.find("/textures/") != std::string::npos ||
            wholePath.find("/sound/") != std::string::npos || wholePath.find("/music/") != std::string::npos ||
            wholePath.find("/video/") != std::string::npos || wholePath.find("/strings/") != std::string::npos ||
            wholePath.find("/shadersfx/") != std::string::npos || wholePath.find("/grass/") != std::string::npos ||
            wholePath.find("/lodsettings/") != std::string::npos || wholePath.find("/facegen/") != std::string::npos ||
            wholePath.find("/screenshots/") != std::string::npos || wholePath.find("/documentation/") != std::string::npos ||
            wholePath.find("/docs/") != std::string::npos || wholePath.find("/licenses/") != std::string::npos ||
            wholePath.find("/readme/") != std::string::npos ||
            wholePath.find("/crashlogs/") != std::string::npos || wholePath.find("/logs/") != std::string::npos ||
            wholePath.find("/animations/") != std::string::npos || wholePath.find("/nemesis_engine/") != std::string::npos ||
            wholePath.find("/calientetools/") != std::string::npos || wholePath.find("/bodyslide/") != std::string::npos ||
            wholePath.find("/dyndolod/") != std::string::npos || wholePath.find("/texgen/") != std::string::npos ||
            wholePath.find("/xlodgen/") != std::string::npos || wholePath.find("/shadercache/") != std::string::npos ||
            wholePath.find("/poserhotkeys/poserdata/") != std::string::npos;
    }

    bool IsNonRuntimeDirectory(const std::filesystem::path& path)
    {
        const auto wholePath = Lower(UHI::PathToUtf8(path)) + "/";
        return wholePath.find("/translations/") != std::string::npos || wholePath.find("/language/") != std::string::npos ||
            wholePath.find("/lang/") != std::string::npos || wholePath.find("/savedsettings/") != std::string::npos ||
            wholePath.find("/localization/") != std::string::npos || wholePath.find("/themes/") != std::string::npos ||
            wholePath.find("/styles/") != std::string::npos || wholePath.find("/presets/") != std::string::npos ||
            wholePath.find("/backup/") != std::string::npos || wholePath.find("/backups/") != std::string::npos ||
            wholePath.find("/meshes/") != std::string::npos || wholePath.find("/textures/") != std::string::npos ||
            wholePath.find("/sound/") != std::string::npos || wholePath.find("/music/") != std::string::npos ||
            wholePath.find("/video/") != std::string::npos || wholePath.find("/strings/") != std::string::npos ||
            wholePath.find("/shadersfx/") != std::string::npos || wholePath.find("/grass/") != std::string::npos ||
            wholePath.find("/lodsettings/") != std::string::npos || wholePath.find("/facegen/") != std::string::npos ||
            wholePath.find("/screenshots/") != std::string::npos || wholePath.find("/documentation/") != std::string::npos ||
            wholePath.find("/docs/") != std::string::npos || wholePath.find("/licenses/") != std::string::npos ||
            wholePath.find("/readme/") != std::string::npos ||
            wholePath.find("/crashlogs/") != std::string::npos || wholePath.find("/logs/") != std::string::npos ||
            wholePath.find("/animations/") != std::string::npos || wholePath.find("/nemesis_engine/") != std::string::npos ||
            wholePath.find("/calientetools/") != std::string::npos || wholePath.find("/bodyslide/") != std::string::npos ||
            wholePath.find("/dyndolod/") != std::string::npos || wholePath.find("/texgen/") != std::string::npos ||
            wholePath.find("/xlodgen/") != std::string::npos || wholePath.find("/shadercache/") != std::string::npos ||
            wholePath.find("/poserhotkeys/poserdata/") != std::string::npos;
    }

    bool IsEffectiveRuntimeConfig(const std::filesystem::path& path)
    {
        const auto value = Lower(UHI::PathToUtf8(path.lexically_normal()));
        if (value.find("::") != std::string::npos || IsNonRuntimeResource(path)) return false;
        // A number of native plugins deliberately load settings from their
        // own arbitrary Data subdirectory rather than SKSE/Plugins. The scan
        // roots already restrict this parser to the active virtual game tree,
        // so a supported configuration extension plus the resource/default
        // exclusions above is the generic evidence we need here.
        return IsConfig(path);
    }

    bool IsOrphanedLegacySexLabPPlusSettings(const std::filesystem::path& path)
    {
        const auto value = Lower(UHI::PathToUtf8(path.lexically_normal()));
        if (!value.ends_with("/skse/sexlab/settings.yaml") &&
            !value.ends_with("/skse/sexlab/settings.yml")) return false;

        const auto skseRoot = path.parent_path().parent_path();
        std::error_code error;
        return !std::filesystem::is_regular_file(skseRoot / "Plugins" / "SexLab.ini", error);
    }

    bool IsDirectlyEditableConfig(const std::filesystem::path& path)
    {
        if (!IsEffectiveRuntimeConfig(path)) return false;
        const auto extension = Lower(UHI::PathToUtf8(path.extension()));
        return extension == ".ini" || extension == ".json" || extension == ".jsonc" ||
            extension == ".toml" || extension == ".yaml" || extension == ".yml" ||
            extension == ".cfg" || extension == ".conf" ||
            extension == ".settings" || extension == ".properties";
    }

    bool HasUserConfigBesideDefault(const std::filesystem::path& path)
    {
        auto filename = UHI::PathToUtf8(path.filename());
        auto lowered = Lower(filename);
        for (const std::string_view marker : { ".defaults.", ".default.", "_defaults.", "_default.",
                 "-defaults.", "-default." }) {
            const auto position = lowered.find(marker);
            if (position == std::string::npos) continue;
            filename.replace(position, marker.size(), ".");
            std::error_code error;
            return std::filesystem::exists(path.parent_path() / filename, error) && !error;
        }
        return false;
    }

    bool CommentedAssignment(const std::string& content, const std::size_t position)
    {
        const auto newline = content.rfind('\n', position);
        auto start = newline == std::string::npos ? 0 : newline + 1;
        while (start < position && std::isspace(static_cast<unsigned char>(content[start]))) ++start;
        return start < position && (content[start] == '#' || content[start] == ';' ||
            (content[start] == '/' && start + 1 < position && content[start + 1] == '/'));
    }

    std::string McmOwner(const std::filesystem::path& path)
    {
        std::vector<std::filesystem::path> parts;
        for (const auto& part : path) parts.push_back(part);
        for (std::size_t index = 0; index + 2 < parts.size(); ++index) {
            if (Lower(UHI::PathToUtf8(parts[index])) != "mcm") continue;
            const auto kind = Lower(UHI::PathToUtf8(parts[index + 1]));
            if (kind == "config") return UHI::PathToUtf8(parts[index + 2]);
            if (kind == "settings") return UHI::PathToUtf8(path.stem());
        }
        return {};
    }

    std::string ConfigOwner(const std::filesystem::path& path)
    {
        if (auto owner = McmOwner(path); !owner.empty()) return owner;
        std::vector<std::filesystem::path> parts;
        for (const auto& part : path) parts.push_back(part);
        for (std::size_t index = 0; index + 1U < parts.size(); ++index) {
            if (Lower(UHI::PathToUtf8(parts[index])) != "storageutildata") continue;
            const auto& next = parts[index + 1U];
            // StorageUtilData is PapyrusUtil's shared container. The first
            // child is the owning mod's namespace; for a root-level file the
            // filename itself is the only stable identity available.
            return index + 1U == parts.size() - 1U || next.has_extension() ?
                UHI::PathToUtf8(next.stem()) : UHI::PathToUtf8(next);
        }
        for (std::size_t index = 0; index + 1U < parts.size(); ++index) {
            if (Lower(UHI::PathToUtf8(parts[index])) == "interface") {
                return UHI::PathToUtf8(parts[index + 1U]);
            }
        }
        for (std::size_t index = 0; index + 1U < parts.size(); ++index) {
            if (Lower(UHI::PathToUtf8(parts[index])) != "skse" ||
                Lower(UHI::PathToUtf8(parts[index + 1U])) != "plugins" || index + 2U >= parts.size()) continue;
            const auto candidate = parts[index + 2U];
            return candidate == path.filename() ? UHI::PathToUtf8(path.stem()) : UHI::PathToUtf8(candidate);
        }
        auto stem = UHI::PathToUtf8(path.stem());
        const auto lowered = Lower(stem);
        static const std::unordered_set<std::string> genericNames{
            "config", "configuration", "settings", "setting", "preferences", "defaults", "default", "input",
            "inputs", "keybind", "keybinds", "keybinding", "keybindings", "hotkey", "hotkeys", "bindings"
        };
        if (genericNames.contains(lowered) && path.has_parent_path()) {
            auto parentPath = path.parent_path();
            while (!parentPath.empty()) {
                const auto parent = UHI::PathToUtf8(parentPath.filename());
                const auto parentLower = Lower(parent);
                if (!parent.empty() && parentLower != "data" && !genericNames.contains(parentLower)) return parent;
                const auto next = parentPath.parent_path();
                if (next == parentPath) break;
                parentPath = next;
            }
        }
        return stem;
    }

    std::string EmbeddedOwner(const std::string_view content)
    {
        static const std::regex modName(
            R"((?:["']?modName["']?)\s*[:=]\s*["']([^"'\r\n]{2,96})["'])",
            std::regex::icase);
        std::match_results<std::string_view::const_iterator> match;
        if (std::regex_search(content.begin(), content.end(), match, modName)) {
            return UHI::CanonicalModDisplayName(match[1].str());
        }
        static const std::regex plugin(
            R"(([A-Za-z0-9 _.'+\-]{2,96})\.(?:esp|esm|esl)\s*\|)", std::regex::icase);
        using Iterator = std::string_view::const_iterator;
        for (std::regex_iterator<Iterator> iterator(content.begin(), content.end(), plugin), end;
             iterator != end; ++iterator) {
            auto candidate = Trim((*iterator)[1].str());
            const auto lowered = Lower(candidate);
            if (lowered == "skyrim" || lowered == "update" || lowered == "dawnguard" ||
                lowered == "hearthfires" || lowered == "dragonborn") continue;
            return UHI::CanonicalModDisplayName(std::move(candidate));
        }
        return {};
    }

    bool InMcmKind(const std::filesystem::path& path, const std::string_view kind)
    {
        const auto lowered = Lower(UHI::PathToUtf8(path));
        return lowered.find("/mcm/" + std::string(kind) + "/") != std::string::npos;
    }
}

namespace UHI::Scanners
{
    bool GenericConfigScanner::MayContainBinding(const std::string_view bytes) noexcept
    {
        try {
            std::string decoded;
            auto content = bytes;
            if (bytes.find('\0') != std::string_view::npos) {
                decoded = UHI::DecodeText(std::span(bytes.data(), bytes.size()));
                content = decoded;
            }
            static constexpr std::array<std::string_view, 17> indicators{
                "hotkey", "keybind", "shortcut", "keycode", "scancode", "inputkey",
                "virtualkey", "keymap", "togglekey", "dik", "keycustom", "keydefine",
                "primarykey", "menukey", "keyboardkey", "gamepadkey", "buttonmap"
            };
            if (std::ranges::any_of(indicators, [&](const auto value) {
                    return ContainsAsciiInsensitive(content, value);
                })) return true;
            // Some native plugins place ordinary action names under a
            // dedicated input section, for example `[Keys] HideWidgets=13`.
            // The section supplies the input semantics even though the member
            // name itself does not contain Key/Hotkey.
            static const std::regex bindingSection(
                R"((?:^|[\r\n])\s*\[(?:keys?|hotkeys?|keybinds?|bindings?|shortcuts?|inputs?|keyboard|mouse|gamepad|controller|controls?)\]\s*(?:[\r\n]|$))",
                std::regex::icase);
            if (std::regex_search(content.begin(), content.end(), bindingSection)) return true;
            const auto assignmentIdentifier = [&] {
                std::size_t position{};
                while (position < content.size()) {
                    while (position < content.size() &&
                        !(std::isalnum(static_cast<unsigned char>(content[position])) ||
                            content[position] == '_' || content[position] == '-' || content[position] == '.')) ++position;
                    const auto start = position;
                    while (position < content.size() &&
                        (std::isalnum(static_cast<unsigned char>(content[position])) ||
                            content[position] == '_' || content[position] == '-' || content[position] == '.')) ++position;
                    if (position == start) continue;
                    const auto identifier = content.substr(start, position - start);
                    const bool inputName = LooksLikeBinding(std::string(identifier));
                    if (!inputName) continue;
                    auto cursor = position;
                    if (cursor < content.size() && (content[cursor] == '\"' || content[cursor] == '\'')) ++cursor;
                    while (cursor < content.size() && std::isspace(static_cast<unsigned char>(content[cursor]))) ++cursor;
                    if (cursor < content.size() && (content[cursor] == ':' || content[cursor] == '=')) return true;
                    // XML represents the setting name either as the element
                    // name (<KeyCustom value="F2"/>) or as an attribute value
                    // (<Setting name="KeyDefine" value="F3"/>). In both
                    // forms the input-like identifier is not itself followed
                    // by ':' or '=', so let the bounded XML parser inspect it.
                    if (content.find('<') != std::string_view::npos) return true;
                }
                return false;
            };
            return assignmentIdentifier();
        } catch (...) {
            // A failed prefilter must fall back to the complete parser.
            return true;
        }
    }

    std::vector<HotkeyRecord> GenericConfigScanner::ScanContent(const std::filesystem::path& source,
        const std::string_view utf8Content, const std::string_view ownerOverride,
        const NumericCodeSpace numericFallback) const
    {
        struct Assignment
        {
            std::string key;
            std::string raw;
            std::size_t position{};
            std::string_view context;
        };
        enum class CompoundRole { none, primary, modifier };
        struct CompoundMember
        {
            CompoundRole role{ CompoundRole::none };
            std::string family;
            std::string actionKey;
            std::string device;
            int order{};
        };

        const auto compoundMember = [](const std::string& key) {
            const auto lowered = Lower(key);
            const auto describe = [&](const std::string_view suffix, const CompoundRole role,
                                      const std::string_view device, const int order) -> std::optional<CompoundMember> {
                if (!lowered.ends_with(suffix) || key.size() <= suffix.size()) return std::nullopt;
                auto actionKey = key.substr(0, key.size() - suffix.size());
                while (!actionKey.empty() && (actionKey.back() == '_' || actionKey.back() == '-' ||
                    actionKey.back() == '.')) actionKey.pop_back();
                if (actionKey.empty()) return std::nullopt;
                auto family = Lower(actionKey);
                std::erase_if(family, [](const unsigned char character) { return !std::isalnum(character); });
                if (family.empty()) return std::nullopt;
                return CompoundMember{ role, std::move(family), std::move(actionKey), std::string(device), order };
            };
            // Longest suffixes come first. These names are common serialized
            // setting conventions, not mod-specific identifiers.
            for (const auto& [suffix, role, device, order] : std::array{
                     std::tuple{ std::string_view("_gamepadmodifier1"), CompoundRole::modifier, std::string_view("gamepad"), 1 },
                     std::tuple{ std::string_view("_gamepadmodifier2"), CompoundRole::modifier, std::string_view("gamepad"), 2 },
                     std::tuple{ std::string_view("_gamepadmod1"), CompoundRole::modifier, std::string_view("gamepad"), 1 },
                     std::tuple{ std::string_view("_gamepadmod2"), CompoundRole::modifier, std::string_view("gamepad"), 2 },
                     std::tuple{ std::string_view("_gpmodifier1"), CompoundRole::modifier, std::string_view("gamepad"), 1 },
                     std::tuple{ std::string_view("_gpmodifier2"), CompoundRole::modifier, std::string_view("gamepad"), 2 },
                     std::tuple{ std::string_view("_gpmod1"), CompoundRole::modifier, std::string_view("gamepad"), 1 },
                     std::tuple{ std::string_view("_gpmod2"), CompoundRole::modifier, std::string_view("gamepad"), 2 },
                     std::tuple{ std::string_view("_keyboardmodifier1"), CompoundRole::modifier, std::string_view("keyboard"), 1 },
                     std::tuple{ std::string_view("_keyboardmodifier2"), CompoundRole::modifier, std::string_view("keyboard"), 2 },
                     std::tuple{ std::string_view("_modifier1"), CompoundRole::modifier, std::string_view("keyboard"), 1 },
                     std::tuple{ std::string_view("_modifier2"), CompoundRole::modifier, std::string_view("keyboard"), 2 },
                     std::tuple{ std::string_view("_mod1"), CompoundRole::modifier, std::string_view("keyboard"), 1 },
                     std::tuple{ std::string_view("_mod2"), CompoundRole::modifier, std::string_view("keyboard"), 2 },
                     std::tuple{ std::string_view("_gamepadkey"), CompoundRole::primary, std::string_view("gamepad"), 0 },
                     std::tuple{ std::string_view("_controllerkey"), CompoundRole::primary, std::string_view("gamepad"), 0 },
                     std::tuple{ std::string_view("_gpkey"), CompoundRole::primary, std::string_view("gamepad"), 0 },
                     std::tuple{ std::string_view("_padkey"), CompoundRole::primary, std::string_view("gamepad"), 0 },
                     std::tuple{ std::string_view("_keyboardkey"), CompoundRole::primary, std::string_view("keyboard"), 0 },
                     std::tuple{ std::string_view("_kbkey"), CompoundRole::primary, std::string_view("keyboard"), 0 },
                     std::tuple{ std::string_view("_key"), CompoundRole::primary, std::string_view("keyboard"), 0 }
                 }) {
                if (auto member = describe(suffix, role, device, order)) return *member;
            }
            return CompoundMember{};
        };

        std::vector<HotkeyRecord> results;
        if (!MayContainBinding(utf8Content)) return results;
        const std::string content(utf8Content);
        const std::string owner = [&] {
            if (auto embedded = EmbeddedOwner(utf8Content); !embedded.empty()) return embedded;
            return UHI::CanonicalModDisplayName(ownerOverride.empty() ?
                ConfigOwner(source) : std::string(ownerOverride));
        }();
        std::unordered_set<std::string> seen;

        const auto add = [&](const std::string& key, const std::string& raw, const std::size_t offset,
                             const std::string_view context, const std::string_view actionKey = {},
                             const std::vector<std::string>* compoundModifiers = nullptr,
                             const NumericCodeSpace structuralCodeSpace = NumericCodeSpace::unknown) {
            if (results.size() >= kMaximumCollectedRecords) return;
            const auto bindingSection = IsBindingSection(NearbySection(utf8Content, offset));
            if ((!LooksLikeBinding(key) && !bindingSection) || IsDisabled(raw) ||
                !BareBindingNameHasMeaning(key, context, utf8Content, offset)) return;
            auto codeSpace = InferNumericCodeSpace(key);
            bool fallbackUsed = false;
            if (codeSpace == NumericCodeSpace::unknown) codeSpace = structuralCodeSpace;
            if (codeSpace == NumericCodeSpace::unknown) codeSpace = InferContextCodeSpace(context);
            if (codeSpace == NumericCodeSpace::unknown) {
                codeSpace = InferPathCodeSpace(source);
                fallbackUsed = codeSpace != NumericCodeSpace::unknown;
            }
            if (codeSpace == NumericCodeSpace::unknown) {
                codeSpace = numericFallback;
                fallbackUsed = codeSpace != NumericCodeSpace::unknown;
            }
            auto parsed = ParseConfigBinding(key, raw, codeSpace);
            // An unresolved string or ambiguous number is evidence that a
            // file mentions input, not proof of an active physical shortcut.
            if (!parsed.conflictEligible) return;
            if (fallbackUsed && parsed.conflictEligible) parsed.codeSystem += " (source-convention fallback)";
            parsed.binding = ApplyModifiers(std::move(parsed.binding), context, source, key);
            if (compoundModifiers && !compoundModifiers->empty()) {
                std::string chord;
                for (const auto& modifier : *compoundModifiers) {
                    if (modifier.empty()) continue;
                    if (!chord.empty()) chord += '+';
                    chord += modifier;
                }
                if (!chord.empty()) parsed.binding = chord + '+' + parsed.binding;
            }
            if (parsed.binding.empty()) return;
            const auto action = ResolveActionName(actionKey.empty() ? key : std::string(actionKey),
                context, utf8Content, offset, owner);
            const auto activation = InferActivationContext(
                std::string(context) + " " + key, ContextEvidenceSource::structuredConfiguration);
            const auto line = LineAt(utf8Content, offset);
            const auto identity = key + '\x1F' + parsed.binding + '\x1F' + std::to_string(line);
            if (!seen.insert(identity).second) return;
            results.push_back({
                .owner = owner,
                .action = action,
                .binding = parsed.binding,
                .rawBinding = raw,
                .settingName = key,
                .settingSection = NearbySection(utf8Content, offset),
                .codeSystem = parsed.codeSystem,
                .device = parsed.device,
                .detector = "StructuredConfigScanner",
                .confidence = parsed.conflictEligible ? Confidence::inferred : Confidence::candidate,
                .evidencePath = source,
                .evidenceLine = line,
                .stage = ScanStage::configuration,
                .editable = IsDirectlyEditableConfig(source),
                .runtimeActive = IsEffectiveRuntimeConfig(source),
                .conflictEligible = parsed.conflictEligible,
                .contextMask = activation.mask,
                .contextConfidence = activation.confidence
            });
        };

        // Handles INI/TOML/YAML plus any number of members on one JSON line.
        static const std::regex assignment(
            R"UHI((?:"([^"]{1,128})"|'([^']{1,128})'|([A-Za-z0-9_.-]+))\s*[:=]\s*(?:"([^"]*)"|'([^']*)'|([^,\s{}\]#;<>"']+)))UHI",
            std::regex::icase);
        std::vector<Assignment> assignments;
        for (std::sregex_iterator iterator(content.begin(), content.end(), assignment), end; iterator != end; ++iterator) {
            const auto& match = *iterator;
            const auto key = match[1].matched ? match[1].str() : match[2].matched ? match[2].str() : match[3].str();
            const auto raw = match[4].matched ? match[4].str() : match[5].matched ? match[5].str() : match[6].str();
            const auto position = static_cast<std::size_t>(match.position());
            if (CommentedAssignment(content, position) ||
                (!match[4].matched && !match[5].matched && !raw.empty() &&
                    (raw.front() == '{' || raw.front() == '['))) continue;
            const auto contextStart = position > 192 ? position - 192 : 0;
            const auto contextLength = std::min<std::size_t>(512, content.size() - contextStart);
            assignments.push_back({ key, raw, position,
                std::string_view(content).substr(contextStart, contextLength) });
        }

        std::vector<CompoundMember> compoundMembers;
        compoundMembers.reserve(assignments.size());
        std::unordered_map<std::string, std::vector<std::size_t>> compoundFamilies;
        for (std::size_t index = 0; index < assignments.size(); ++index) {
            compoundMembers.push_back(compoundMember(assignments[index].key));
            if (compoundMembers.back().role != CompoundRole::none)
                compoundFamilies[compoundMembers.back().family].push_back(index);
        }
        const auto isCompoundFamily = [&](const std::string& family) {
            const auto found = compoundFamilies.find(family);
            if (found == compoundFamilies.end()) return false;
            bool primary = false;
            bool modifier = false;
            std::unordered_set<std::string> primaryDevices;
            for (const auto index : found->second) {
                const auto& member = compoundMembers[index];
                primary |= member.role == CompoundRole::primary;
                modifier |= member.role == CompoundRole::modifier;
                if (member.role == CompoundRole::primary) primaryDevices.insert(member.device);
            }
            return primary && (modifier || primaryDevices.size() > 1U);
        };
        for (std::size_t index = 0; index < assignments.size(); ++index) {
            if (results.size() >= kMaximumCollectedRecords) break;
            const auto& value = assignments[index];
            const auto& member = compoundMembers[index];
            if (member.role == CompoundRole::modifier && isCompoundFamily(member.family)) continue;
            if (member.role != CompoundRole::primary || !isCompoundFamily(member.family)) {
                add(value.key, value.raw, value.position, value.context);
                continue;
            }

            std::vector<std::pair<int, std::string>> orderedModifiers;
            for (const auto siblingIndex : compoundFamilies[member.family]) {
                const auto& siblingMember = compoundMembers[siblingIndex];
                const auto& sibling = assignments[siblingIndex];
                if (siblingMember.role != CompoundRole::modifier || siblingMember.device != member.device ||
                    IsDisabled(sibling.raw)) continue;
                auto modifierSpace = member.device == "gamepad" ? NumericCodeSpace::skseUnifiedInputCode :
                    InferNumericCodeSpace(value.key);
                if (modifierSpace == NumericCodeSpace::unknown) modifierSpace = InferContextCodeSpace(value.context);
                if (modifierSpace == NumericCodeSpace::unknown) modifierSpace = InferPathCodeSpace(source);
                if (modifierSpace == NumericCodeSpace::unknown) modifierSpace = numericFallback;
                const auto modifier = ParseConfigBinding(member.device == "gamepad" ? "GamepadKey" : "ScanCode",
                    sibling.raw, modifierSpace);
                if (modifier.conflictEligible && !modifier.binding.empty())
                    orderedModifiers.emplace_back(siblingMember.order, modifier.binding);
            }
            std::ranges::sort(orderedModifiers, {}, &std::pair<int, std::string>::first);
            std::vector<std::string> modifiers;
            modifiers.reserve(orderedModifiers.size());
            for (auto& [order, modifier] : orderedModifiers) modifiers.push_back(std::move(modifier));
            // A sibling family containing `_Key`/`_Mod*` and `_GPKey`/`_GPMod*`
            // is itself sufficient evidence for the unified SKSE input number
            // convention.  This keeps arbitrary nested `keybinds.ini` files
            // decodable even when the caller cannot supply a path-based hint.
            add(value.key, value.raw, value.position, value.context, member.actionKey, &modifiers,
                NumericCodeSpace::skseUnifiedInputCode);
        }

        // XML element/attribute forms: <Hotkey value="68"/> and
        // <Setting name="Hotkey" value="68"/>.
        static const std::regex xmlDirect(
            R"(<\s*([A-Za-z0-9_.-]*(?:hotkey|keybind|shortcut|keycode|key|button|bind)[A-Za-z0-9_.-]*)\b[^>]*(?:value|code|key)\s*=\s*["']([^"']+)["'][^>]*>)",
            std::regex::icase);
        static const std::regex xmlSetting(
            R"(<\s*(?:setting|option|entry)\b[^>]*(?:name|id)\s*=\s*["']([^"']+)["'][^>]*(?:value|default)\s*=\s*["']([^"']+)["'][^>]*>)",
            std::regex::icase);
        for (const auto* expression : { &xmlDirect, &xmlSetting }) {
            for (std::sregex_iterator iterator(content.begin(), content.end(), *expression), end; iterator != end; ++iterator) {
                if (results.size() >= kMaximumCollectedRecords) break;
                const auto position = static_cast<std::size_t>(iterator->position());
                add((*iterator)[1].str(), (*iterator)[2].str(), position, iterator->str());
            }
            if (results.size() >= kMaximumCollectedRecords) break;
        }
        return results;
    }

    std::vector<HotkeyRecord> GenericConfigScanner::Scan(const std::filesystem::path& root,
        const bool recursive, const CancelCallback& cancel, const PathCallback& path,
        const NumericCodeSpace numericFallback, ScanCache* cache, const std::size_t workers,
        const ItemProgressCallback& itemProgress) const
    {
        std::vector<HotkeyRecord> results;
        std::error_code error;
        if (!std::filesystem::exists(root, error)) return results;
        std::vector<std::filesystem::path> candidates;
        const auto collectEntry = [&](const std::filesystem::directory_entry& entry) {
            std::error_code entryError;
            if (!entry.is_regular_file(entryError) || entryError || !IsConfig(entry.path()) ||
                UHI::IsKnownBethesdaGameFile(entry.path()) || HasSpecializedScanner(entry.path()) ||
                IsNonRuntimeResource(entry.path()) || !IsEffectiveRuntimeConfig(entry.path()) ||
                IsOrphanedLegacySexLabPPlusSettings(entry.path()) ||
                HasUserConfigBesideDefault(entry.path())) return;
            const auto fileSize = entry.file_size(entryError);
            if (!entryError && fileSize <= 16 * 1024 * 1024) candidates.push_back(entry.path());
        };
        if (recursive) {
            std::filesystem::recursive_directory_iterator iterator(root,
                std::filesystem::directory_options::skip_permission_denied, error), end;
            while (iterator != end) {
                if (ScanCancelled(cancel)) break;
                if (!error) {
                    try {
                        std::error_code entryError;
                        if (iterator->is_directory(entryError) && !entryError &&
                            IsNonRuntimeDirectory(iterator->path())) {
                            iterator.disable_recursion_pending();
                        } else {
                            collectEntry(*iterator);
                        }
                    } catch (...) {}
                }
                error.clear();
                iterator.increment(error);
                if (error) error.clear();
            }
        } else {
            std::filesystem::directory_iterator iterator(root,
                std::filesystem::directory_options::skip_permission_denied, error), end;
            while (iterator != end) {
                if (ScanCancelled(cancel)) break;
                if (!error) {
                    try { collectEntry(*iterator); } catch (...) {}
                }
                error.clear();
                iterator.increment(error);
                if (error) error.clear();
            }
        }
        std::vector<std::vector<HotkeyRecord>> perFile(candidates.size());
        std::atomic_size_t next{}, finished{};
        std::mutex progressMutex;
        const auto fallbackTag = std::to_string(static_cast<unsigned>(numericFallback));
        const auto worker = [&] {
            UHI::EnterBackgroundScanMode();
            while (!ScanCancelled(cancel)) {
                const auto index = next.fetch_add(1);
                if (index >= candidates.size()) break;
                const auto& source = candidates[index];
                {
                    std::scoped_lock lock(progressMutex);
                    ReportScanPath(path, source);
                    ReportItemProgress(itemProgress, source, finished.load(), candidates.size(), 0.0F);
                }
                // Bump whenever parsing/detection semantics change so an old
                // negative result cannot hide a newly supported binding form.
                const auto tag = "config-v16-generic-compound-" + fallbackTag;
                if (cache) {
                    if (auto cached = cache->Find(tag, source)) {
                        perFile[index] = std::move(*cached);
                        const auto done = finished.fetch_add(1) + 1;
                        std::scoped_lock lock(progressMutex);
                        ReportItemProgress(itemProgress, source, done, candidates.size(), 100.0F);
                        continue;
                    }
                }
                try {
                    std::ifstream input(source, std::ios::binary);
                    if (input) {
                        // Most non-setting JSON/XML/INI resources can be rejected
                        // from a small prefix. Read the complete file only when
                        // that prefix contains a binding-shaped setting name.
                        constexpr std::size_t kProbeBytes = 256U * 1024U;
                        std::string probe(kProbeBytes, '\0');
                        input.read(probe.data(), static_cast<std::streamsize>(probe.size()));
                        probe.resize(static_cast<std::size_t>(input.gcount()));
                        {
                            std::scoped_lock lock(progressMutex);
                            ReportItemProgress(itemProgress, source, finished.load(), candidates.size(), 35.0F);
                        }
                        if (MayContainBinding(probe)) {
                            input.clear();
                            input.seekg(0, std::ios::beg);
                            const std::string bytes((std::istreambuf_iterator<char>(input)), {});
                            {
                                std::scoped_lock lock(progressMutex);
                                ReportItemProgress(itemProgress, source, finished.load(), candidates.size(), 65.0F);
                            }
                            const auto text = DecodeText(std::span<const char>(bytes.data(), bytes.size()));
                            perFile[index] = ScanContent(source, text, {}, numericFallback);
                        }
                        {
                            std::scoped_lock lock(progressMutex);
                            ReportItemProgress(itemProgress, source, finished.load(), candidates.size(), 95.0F);
                        }
                    }
                } catch (...) {
                    // Isolate disappearing, locked, malformed and hostile files.
                }
                if (cache) cache->Store(tag, source, perFile[index]);
                const auto done = finished.fetch_add(1) + 1;
                {
                    std::scoped_lock lock(progressMutex);
                    ReportItemProgress(itemProgress, source, done, candidates.size(), 100.0F);
                }
            }
        };
        const auto threadCount = std::max<std::size_t>(1, std::min({ workers, candidates.size(), std::size_t{ 4 } }));
        std::vector<std::thread> threads;
        threads.reserve(threadCount);
        for (std::size_t index = 1; index < threadCount; ++index) threads.emplace_back(worker);
        worker();
        for (auto& thread : threads) thread.join();
        for (auto& found : perFile) {
            if (AppendScanResults(results, found)) break;
        }
        // MCM Helper stores shipped defaults in MCM/Config/<mod>/settings.ini
        // and user overrides in MCM/Settings/<mod>.ini. If both provide the
        // same option, only the user value represents the effective binding.
        std::unordered_set<std::string> mcmUserOptions;
        for (const auto& record : results) {
            if (InMcmKind(record.evidencePath, "settings")) {
                mcmUserOptions.insert(Lower(record.owner) + '\x1F' + Lower(record.action));
            }
        }
        std::erase_if(results, [&](const auto& record) {
            return InMcmKind(record.evidencePath, "config") &&
                mcmUserOptions.contains(Lower(record.owner) + '\x1F' + Lower(record.action));
        });
        return results;
    }
}
