#include "UHI/HotkeyRecord.h"
#include "UHI/PathEncoding.h"

#include <algorithm>
#include <cctype>
#include <array>
#include <filesystem>
#include <string_view>
#include <unordered_map>

namespace
{
    std::string Lower(std::string value)
    {
        std::ranges::transform(value, value.begin(), [](const unsigned char character) {
            return static_cast<char>(std::tolower(character));
        });
        return value;
    }

    std::string CompactIdentifier(const std::string_view value)
    {
        std::string result;
        for (const unsigned char character : value) {
            if (std::isalnum(character)) result.push_back(static_cast<char>(std::tolower(character)));
        }
        return result;
    }

    std::string Trim(std::string value)
    {
        const auto notSpace = [](const unsigned char character) { return !std::isspace(character); };
        const auto first = std::ranges::find_if(value, notSpace);
        if (first == value.end()) return {};
        const auto last = std::find_if(value.rbegin(), value.rend(), notSpace).base();
        return std::string(first, last);
    }

    std::string HumanizeIdentifier(std::string value)
    {
        value = Trim(std::move(value));
        if (value.empty()) return value;
        for (const auto suffix : { std::string_view(".esp"), std::string_view(".esm"),
                 std::string_view(".esl"), std::string_view(".bsa"), std::string_view(".dll"),
                 std::string_view(".pex") }) {
            if (Lower(value).ends_with(suffix) && value.size() > suffix.size()) {
                value.resize(value.size() - suffix.size());
                break;
            }
        }
        std::string result;
        result.reserve(value.size() + 12U);
        for (std::size_t index = 0; index < value.size(); ++index) {
            const auto character = static_cast<unsigned char>(value[index]);
            if (character == '_' || character == '-') {
                if (!result.empty() && result.back() != ' ') result.push_back(' ');
                continue;
            }
            const auto previous = index == 0U ? 0U : static_cast<unsigned char>(value[index - 1U]);
            const auto next = index + 1U >= value.size() ? 0U : static_cast<unsigned char>(value[index + 1U]);
            const bool lowerToUpper = index > 0U && std::isupper(character) &&
                (std::islower(previous) || std::isdigit(previous));
            const bool acronymToWord = index > 0U && std::isupper(character) &&
                std::isupper(previous) && next != 0U && std::islower(next);
            if ((lowerToUpper || acronymToWord) && !result.empty() && result.back() != ' ') result.push_back(' ');
            result.push_back(static_cast<char>(character));
        }
        return Trim(std::move(result));
    }

    std::string OwnerFromEvidence(const UHI::HotkeyRecord& record)
    {
        const auto ownerKey = CompactIdentifier(record.owner);
        static const std::array genericOwners{
            std::string_view("keybinds"), std::string_view("config"), std::string_view("configuration"),
            std::string_view("settings"), std::string_view("default"), std::string_view("defaults"),
            std::string_view("input")
        };
        if (std::ranges::find(genericOwners, ownerKey) == genericOwners.end()) return record.owner;

        auto evidence = UHI::PathToUtf8(record.evidencePath.lexically_normal());
        std::ranges::replace(evidence, '\\', '/');
        const auto lowered = Lower(evidence);
        if (const auto marker = lowered.find("/mcm/config/"); marker != std::string::npos) {
            const auto start = marker + 12U;
            const auto end = evidence.find('/', start);
            if (end != std::string::npos && end > start) return evidence.substr(start, end - start);
        }
        if (const auto separator = lowered.find(".bsa::"); separator != std::string::npos) {
            const std::filesystem::path archive(evidence.substr(0, separator + 4U));
            if (!archive.stem().empty()) return UHI::PathToUtf8(archive.stem());
        }
        return record.owner;
    }

    bool MeaninglessAction(const std::string& value)
    {
        const auto trimmed = Trim(value);
        const auto compact = CompactIdentifier(trimmed);
        if (trimmed.size() <= 1U || compact.empty()) return true;
        static const std::array generic{
            std::string_view("key"), std::string_view("keys"), std::string_view("keybind"),
            std::string_view("keybinds"), std::string_view("keycode"), std::string_view("scancode"),
            std::string_view("dxscancode"), std::string_view("dxscancodereference"),
            std::string_view("gamepad"), std::string_view("keyboard"), std::string_view("input")
        };
        if (std::ranges::find(generic, compact) != generic.end()) return true;
        const auto lowered = Lower(trimmed);
        return lowered.find("http://") != std::string::npos || lowered.find("https://") != std::string::npos ||
            lowered.find("creationkit.com") != std::string::npos || lowered.find("uesp.net") != std::string::npos ||
            lowered.find("geckwiki.com") != std::string::npos || lowered.find("dxscan") != std::string::npos ||
            lowered.starts_with("keycode=") || lowered.starts_with("scan code") ||
            lowered.starts_with("gamepad scan code");
    }

    std::string HumanizeLocalizationToken(std::string value)
    {
        if (value.empty() || value.front() != '$') return value;
        while (!value.empty() && value.front() == '$') value.erase(value.begin());

        // SkyUI translation keys normally start with a short mod namespace,
        // e.g. mHUD_ActivationHotKey or OSL_ShowArousalKey.
        if (const auto separator = value.find('_'); separator != std::string::npos &&
            separator > 0U && separator <= 12U) {
            value.erase(0, separator + 1U);
        }

        std::string result;
        result.reserve(value.size() + 8U);
        for (std::size_t index = 0; index < value.size(); ++index) {
            const auto character = static_cast<unsigned char>(value[index]);
            if (character == '_' || character == '-' || character == '.') {
                if (!result.empty() && result.back() != ' ') result.push_back(' ');
                continue;
            }
            const auto previous = index == 0U ? 0U : static_cast<unsigned char>(value[index - 1U]);
            const auto next = index + 1U >= value.size() ? 0U : static_cast<unsigned char>(value[index + 1U]);
            const bool lowerToUpper = index > 0U && std::isupper(character) &&
                (std::islower(previous) || std::isdigit(previous));
            const bool acronymToWord = index > 0U && std::isupper(character) &&
                std::isupper(previous) && next != 0U && std::islower(next);
            if ((lowerToUpper || acronymToWord) && !result.empty() && result.back() != ' ') result.push_back(' ');
            result.push_back(static_cast<char>(character));
        }

        while (!result.empty() && result.front() == ' ') result.erase(result.begin());
        while (!result.empty() && result.back() == ' ') result.pop_back();
        for (const auto prefix : { std::string_view("Hotkey "), std::string_view("Hot Key "),
                 std::string_view("Keymap "), std::string_view("Key Map "),
                 std::string_view("Shortcut "), std::string_view("Key ") }) {
            if (result.size() > prefix.size() && result.starts_with(prefix)) {
                result.erase(0, prefix.size());
                break;
            }
        }
        for (std::size_t position{}; (position = result.find("Hot Key", position)) != std::string::npos;) {
            result.replace(position, 7U, "Hotkey");
            position += 6U;
        }
        return result;
    }

    bool IsSexLabPPlusSettings(const std::filesystem::path& path)
    {
        const auto value = Lower(UHI::PathToUtf8(path.lexically_normal()));
        return value.ends_with("/skse/plugins/sexlabdata/settings.yaml") ||
            value.ends_with("/skse/plugins/sexlabdata/settings.yml") ||
            value.ends_with("/skse/sexlab/settings.yaml") ||
            value.ends_with("/skse/sexlab/settings.yml");
    }

    bool IsOrphanedLegacySexLabPPlusSettings(const std::filesystem::path& path)
    {
        const auto value = Lower(UHI::PathToUtf8(path.lexically_normal()));
        if (!value.ends_with("/skse/sexlab/settings.yaml") &&
            !value.ends_with("/skse/sexlab/settings.yml")) return false;

        // Older P+ builds stored their persistent MCM data here. Vanilla
        // SexLab 1.63 does not consume this file, so an old copy in Overwrite
        // must not be presented as a loaded binding after returning to vanilla.
        const auto skseRoot = path.parent_path().parent_path();
        std::error_code error;
        return !std::filesystem::is_regular_file(skseRoot / "Plugins" / "SexLab.ini", error);
    }
}

namespace UHI
{
    std::string CanonicalModDisplayName(std::string value)
    {
        value = Trim(std::move(value));
        const auto key = CompactIdentifier(value);
        static const std::unordered_map<std::string, std::string> known{
            { "universalhotkeymanagerforskyrimseae", "Universal Hotkey Manager for Skyrim SE-AE" },
            { "skyrimdefaultcontrols", "Skyrim Controls" },
            { "skyrimcontrols", "Skyrim Controls" },
            { "dkaf", "Dynamic Key Action Framework NG" },
            { "dynamickeyactionframeworkng", "Dynamic Key Action Framework NG" },
            { "powershot", "Dynamic Power Shot" },
            { "bowrapidcombo", "Bow Rapid Combo" },
            { "torchescandlelightlanterns", "Torches Candlelight and Lanterns" },
            { "dynamicarmormenu", "Dynamic Armor Variants" },
            { "ultimateimmersiontoggle", "Ultimate Immersion Toggle" },
            { "sksemenuframework", "SKSE Menu Framework" },
            { "ssedisplaytweaks", "SSE Display Tweaks" },
            { "openanimationreplacer", "Open Animation Replacer" },
            { "betterthirdpersonselection", "Better Third Person Selection" },
            { "skyrimfittingsystem", "Skyrim Fitting System" },
            { "skyrimvanitysystem", "Skyrim Vanity System" },
            { "skyrimtransmog", "Skyrim Transmog" },
            { "consoleplusplus", "ConsolePlusPlus" },
            { "po3consoleplusplus", "ConsolePlusPlus" },
            { "po3useortake", "Use or Take" },
            { "scatteredobjectsolution", "Scattered Object Solution" },
            { "ccsb", "Complete Controller Setup" },
            { "sos", "Schlongs of Skyrim" },
            { "obody", "OBody" },
            { "morehud", "moreHUD" },
            { "sexlabutilityplus", "SexLab Utility Plus" },
            { "privateneedsorgasm", "Private Needs - Orgasm" },
            { "deviousdevices", "Devious Devices" },
            { "dd", "Devious Devices" }
        };
        if (const auto found = known.find(key); found != known.end()) return found->second;
        return HumanizeIdentifier(std::move(value));
    }

    std::string CompactActionLabel(const std::string& action)
    {
        auto compact = action;
        const auto trim = [](std::string& value) {
            while (!value.empty() && std::isspace(static_cast<unsigned char>(value.front()))) {
                value.erase(value.begin());
            }
            while (!value.empty() && (std::isspace(static_cast<unsigned char>(value.back())) ||
                value.back() == '.' || value.back() == ':' || value.back() == ';' || value.back() == ',')) {
                value.pop_back();
            }
        };
        trim(compact);
        if (compact.empty()) return compact;

        auto lowered = Lower(compact);
        // dTryKeyUtil/Stances comments commonly produce labels such as
        // "Custom Hotkeyfor Mid Stance". The useful key-face text is the
        // stance itself; the full source description remains in the tooltip.
        if (const auto stance = lowered.find("stance"); stance != std::string::npos) {
            for (const auto kind : { std::string_view("high"), std::string_view("mid"),
                     std::string_view("low"), std::string_view("neutral") }) {
                const auto position = lowered.rfind(kind, stance);
                if (position != std::string::npos) {
                    compact = compact.substr(position, stance + std::string_view("stance").size() - position);
                    trim(compact);
                    compact.front() = static_cast<char>(std::toupper(static_cast<unsigned char>(compact.front())));
                    return compact;
                }
            }
        }
        // Device-qualified setting descriptions such as "Menu toggle key for
        // keyboard" describe the action before the storage/device suffix.
        // Strip that suffix before the generic "key for ..." extraction so
        // the key face says "Menu toggle", not the unhelpful "Keyboard".
        for (const auto suffix : { std::string_view(" key for keyboard"),
                 std::string_view(" key for mouse"), std::string_view(" key for gamepad"),
                 std::string_view(" button for keyboard"), std::string_view(" button for mouse"),
                 std::string_view(" button for gamepad") }) {
            if (lowered.ends_with(suffix) && compact.size() > suffix.size()) {
                compact.resize(compact.size() - suffix.size());
                trim(compact);
                lowered = Lower(compact);
                break;
            }
        }
        const auto takeAfter = [&](const std::string_view marker) {
            const auto position = lowered.find(marker);
            if (position == std::string::npos) return false;
            compact.erase(0, position + marker.size());
            lowered.erase(0, position + marker.size());
            trim(compact);
            return true;
        };

        // Configuration comments often describe storage mechanics rather than
        // the feature. Keep the full sentence in the tooltip and extract the
        // phrase that tells the player what pressing the key actually does.
        if (!takeAfter("key to ")) {
            if (!takeAfter("button to ")) {
                if (!takeAfter("key for ")) takeAfter("button for ");
            }
        }

        lowered = Lower(compact);
        for (const auto prefix : { std::string_view("be used for "), std::string_view("use for "),
                 std::string_view("used for "), std::string_view("enter the "),
                 std::string_view("enter "), std::string_view("set the ") }) {
            if (lowered.starts_with(prefix) && compact.size() > prefix.size()) {
                compact.erase(0, prefix.size());
                break;
            }
        }

        lowered = Lower(compact);
        std::size_t end = compact.size();
        for (const auto separator : { std::string_view(", then"), std::string_view(" then "),
                 std::string_view(". see "), std::string_view("; see "),
                 std::string_view(" when "), std::string_view(" while "),
                 std::string_view(" so that "), std::string_view("http://"),
                 std::string_view("https://") }) {
            if (const auto position = lowered.find(separator); position != std::string::npos) {
                end = (std::min)(end, position);
            }
        }
        if (const auto period = compact.find('.'); period != std::string::npos) end = (std::min)(end, period);
        if (const auto comma = compact.find(','); comma != std::string::npos) end = (std::min)(end, comma);
        compact.resize(end);
        trim(compact);

        lowered = Lower(compact);
        if (lowered.ends_with(" state") && compact.size() > 6U) compact.resize(compact.size() - 6U);
        trim(compact);
        if (!compact.empty() && compact.size() <= 32U) {
            compact.front() = static_cast<char>(std::toupper(static_cast<unsigned char>(compact.front())));
            return compact;
        }

        // If a sentence did not use the common "key to ..." wording, prefer
        // its first concrete action phrase over a blind character truncation.
        const auto source = Lower(action);
        static constexpr std::array concepts{
            std::pair{ std::string_view("ready state"), std::string_view("Ready") },
            std::pair{ std::string_view("parrying"), std::string_view("Parrying") },
            std::pair{ std::string_view("parry"), std::string_view("Parry") },
            std::pair{ std::string_view("free camera"), std::string_view("Free Camera") },
            std::pair{ std::string_view("screenshot"), std::string_view("Screenshot") },
            std::pair{ std::string_view("quick save"), std::string_view("Quick Save") },
            std::pair{ std::string_view("quick load"), std::string_view("Quick Load") },
            std::pair{ std::string_view("inventory"), std::string_view("Inventory") },
            std::pair{ std::string_view("favorites"), std::string_view("Favorites") },
            std::pair{ std::string_view("overlay"), std::string_view("Overlay") },
            std::pair{ std::string_view("dodge"), std::string_view("Dodge") },
            std::pair{ std::string_view("block"), std::string_view("Block") },
            std::pair{ std::string_view("attack"), std::string_view("Attack") },
            std::pair{ std::string_view("sprint"), std::string_view("Sprint") },
            std::pair{ std::string_view("jump"), std::string_view("Jump") },
            std::pair{ std::string_view("shout"), std::string_view("Shout") }
        };
        std::size_t best = std::string::npos;
        std::string_view label;
        for (const auto& [needle, candidate] : concepts) {
            const auto position = source.find(needle);
            if (position != std::string::npos && position < best) {
                best = position;
                label = candidate;
            }
        }
        if (!label.empty()) return std::string(label);

        if (compact.size() > 32U) {
            const auto boundary = compact.rfind(' ', 31U);
            compact.resize(boundary == std::string::npos ? 32U : boundary);
        }
        trim(compact);
        return compact.empty() ? action : compact;
    }

    void NormalizeRecordDisplayNames(HotkeyRecord& record) noexcept
    {
        try {
            record.owner = CanonicalModDisplayName(OwnerFromEvidence(record));

            const bool sexLabPPlusSettings = IsSexLabPPlusSettings(record.evidencePath);
            if (sexLabPPlusSettings) record.owner = "SexLab P+";
            if (IsOrphanedLegacySexLabPPlusSettings(record.evidencePath)) {
                record.runtimeActive = false;
                record.conflictEligible = false;
            }

            const auto setting = CompactIdentifier(record.settingName);
            if (sexLabPPlusSettings) {
                static constexpr std::array names{
                    std::pair{ std::string_view("ikeyup"), std::string_view("Scene Menu Up") },
                    std::pair{ std::string_view("ikeydown"), std::string_view("Scene Menu Down") },
                    std::pair{ std::string_view("ikeyleft"), std::string_view("Scene Menu Left") },
                    std::pair{ std::string_view("ikeyright"), std::string_view("Scene Menu Right") },
                    std::pair{ std::string_view("ikeyadvance"), std::string_view("Advance Scene / Menu Select") },
                    std::pair{ std::string_view("ikeyend"), std::string_view("End Scene / Close Menu") },
                    std::pair{ std::string_view("ikeyextra2"), std::string_view("Open Scene Menu / Switch Tab") },
                    std::pair{ std::string_view("ikeymod"), std::string_view("Scene Control Modifier") },
                    std::pair{ std::string_view("ikeyreset"), std::string_view("Inverse Scene Action") },
                    std::pair{ std::string_view("itogglefreecamera"), std::string_view("Toggle Free Camera") },
                    std::pair{ std::string_view("itargetactor"), std::string_view("Target Actor for Edit") }
                };
                if (const auto found = std::ranges::find_if(names,
                    [&](const auto& item) { return item.first == setting; }); found != names.end()) {
                    record.action = found->second;
                }
            }

            record.action = HumanizeLocalizationToken(std::move(record.action));
            if (setting.starts_with("ikey") && record.action.size() > 4U &&
                record.action.starts_with("Key ")) {
                record.action.erase(0, 4U);
            }
            if (MeaninglessAction(record.action)) record.action = record.owner;
            if (record.action.empty()) record.action = record.owner;
        } catch (...) {
        }
    }

    std::string ActivationContextLabel(const std::uint32_t mask)
    {
        if (mask == 0) return "Unknown";
        if ((mask & static_cast<std::uint32_t>(ActivationContext::global)) != 0) return "Global";
        static constexpr std::array contexts{
            std::pair{ ActivationContext::gameplay, "Gameplay" },
            std::pair{ ActivationContext::menuMode, "Menu mode" },
            std::pair{ ActivationContext::console, "Console" },
            std::pair{ ActivationContext::itemMenu, "Item menu" },
            std::pair{ ActivationContext::inventory, "Inventory" },
            std::pair{ ActivationContext::favorites, "Favorites" },
            std::pair{ ActivationContext::map, "Map" },
            std::pair{ ActivationContext::stats, "Stats / Magic" },
            std::pair{ ActivationContext::cursor, "Cursor" },
            std::pair{ ActivationContext::book, "Book" },
            std::pair{ ActivationContext::journal, "Journal" },
            std::pair{ ActivationContext::lockpicking, "Lockpicking" },
            std::pair{ ActivationContext::externalOverlay, "External overlay" },
            std::pair{ ActivationContext::debugText, "Debug text" },
            std::pair{ ActivationContext::debugOverlay, "Debug overlay" },
            std::pair{ ActivationContext::tfcMode, "Free camera" },
            std::pair{ ActivationContext::mapDebug, "Debug map" },
            std::pair{ ActivationContext::marketplace, "Creations menu" },
            std::pair{ ActivationContext::favor, "Favor" },
            std::pair{ ActivationContext::sexLabScene, "SexLab scene" }
        };
        std::string result;
        for (const auto& [context, label] : contexts) {
            if ((mask & static_cast<std::uint32_t>(context)) == 0) continue;
            if (!result.empty()) result += ", ";
            result += label;
        }
        return result.empty() ? "Unknown" : result;
    }

    std::string NormalizeBinding(std::string value)
    {
        value.erase(std::remove_if(value.begin(), value.end(), [](const unsigned char ch) {
            return std::isspace(ch) != 0;
        }), value.end());
        std::transform(value.begin(), value.end(), value.begin(), [](const unsigned char ch) {
            return static_cast<char>(std::toupper(ch));
        });
        return value;
    }

    std::string CompactBindingLabel(const std::string& binding)
    {
        static constexpr std::array<std::pair<std::string_view, std::string_view>, 25> replacements{{
            { "Print Screen", "PrtSc" }, { "Scroll Lock", "ScrLk" },
            { "Page Up", "PgUp" }, { "Page Down", "PgDn" },
            { "Backspace", "Bksp" }, { "Insert", "Ins" },
            { "Delete", "Del" }, { "Num Lock", "NumLk" },
            { "Left Control", "LCtrl" }, { "Right Control", "RCtrl" },
            { "Left Ctrl", "LCtrl" }, { "Right Ctrl", "RCtrl" },
            { "Left Shift", "LShift" }, { "Right Shift", "RShift" },
            { "Left Alt", "LAlt" }, { "Right Alt", "RAlt" },
            { "Left Windows", "LWin" }, { "Right Windows", "RWin" },
            { "Windows", "Win" }, { "Escape", "Esc" },
            { "Enter", "Ent" }, { "Numpad Decimal", "Num." },
            { "Numpad ", "Num" },
            { "Left Mouse", "LMB" }, { "Right Mouse", "RMB" }
        }};

        std::string result = binding;
        for (const auto [longName, shortName] : replacements) {
            std::size_t offset = 0;
            while ((offset = result.find(longName, offset)) != std::string::npos) {
                result.replace(offset, longName.size(), shortName);
                offset += shortName.size();
            }
        }
        return result;
    }
}
