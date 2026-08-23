#include "UHI/scanners/ControlMapScanner.h"
#include "UHI/ConfigBindingParser.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <fstream>
#include <sstream>
#include <regex>
#include <string_view>
#include <optional>

namespace
{
    std::string Trim(std::string value)
    {
        const auto space = [](const unsigned char ch) { return std::isspace(ch) != 0; };
        value.erase(value.begin(), std::find_if_not(value.begin(), value.end(), space));
        value.erase(std::find_if_not(value.rbegin(), value.rend(), space).base(), value.end());
        return value;
    }

    bool Disabled(std::string value)
    {
        value = Trim(std::move(value));
        std::ranges::transform(value, value.begin(), [](const unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
        return value.empty() || value == "-1" || value == "0xff" || value == "255";
    }

    std::vector<std::string> Split(const std::string_view value, const char separator)
    {
        std::vector<std::string> parts;
        std::size_t start{};
        while (start <= value.size()) {
            const auto end = value.find(separator, start);
            parts.push_back(Trim(std::string(value.substr(start,
                end == std::string_view::npos ? value.size() - start : end - start))));
            if (end == std::string_view::npos) break;
            start = end + 1;
        }
        return parts;
    }

    std::vector<std::string> SplitTabColumns(const std::string_view value)
    {
        std::vector<std::string> columns;
        std::size_t start{};
        while (start < value.size()) {
            const auto end = value.find('\t', start);
            auto column = Trim(std::string(value.substr(start,
                end == std::string_view::npos ? value.size() - start : end - start)));
            if (!column.empty()) columns.push_back(std::move(column));
            if (end == std::string_view::npos) break;
            start = end + 1U;
        }
        return columns;
    }

    UHI::ParsedConfigBinding ParseExpression(const std::string_view expression, const std::string_view device)
    {
        UHI::ParsedConfigBinding result;
        result.device = std::string(device);
        result.conflictEligible = true;
        for (const auto& component : Split(expression, '+')) {
            const auto parsed = UHI::ParseControlMapInputCode(component, device);
            if (!result.binding.empty()) result.binding += '+';
            result.binding += parsed.binding;
            if (result.codeSystem == "unknown") result.codeSystem = parsed.codeSystem;
            result.conflictEligible = result.conflictEligible && parsed.conflictEligible;
        }
        return result;
    }

    std::optional<UHI::ActivationContext> ContextFromComment(std::string comment)
    {
        comment = Trim(std::move(comment));
        if (comment.starts_with("//")) comment = Trim(comment.substr(2));
        else if (comment.starts_with('#')) comment = Trim(comment.substr(1));
        std::ranges::transform(comment, comment.begin(), [](const unsigned char ch) {
            return static_cast<char>(std::tolower(ch));
        });
        if (comment == "main gameplay") return UHI::ActivationContext::gameplay;
        if (comment == "menu mode") return UHI::ActivationContext::menuMode;
        if (comment == "console") return UHI::ActivationContext::console;
        if (comment == "item menus" || comment == "item menu") return UHI::ActivationContext::itemMenu;
        if (comment == "inventory") return UHI::ActivationContext::inventory;
        if (comment == "debug text") return UHI::ActivationContext::debugText;
        if (comment == "favorites menu" || comment == "favorites") return UHI::ActivationContext::favorites;
        if (comment == "map menu") return UHI::ActivationContext::map;
        if (comment == "stats") return UHI::ActivationContext::stats;
        if (comment == "cursor") return UHI::ActivationContext::cursor;
        if (comment == "book") return UHI::ActivationContext::book;
        if (comment == "debug overlay") return UHI::ActivationContext::debugOverlay;
        if (comment == "journal") return UHI::ActivationContext::journal;
        if (comment == "tfc mode") return UHI::ActivationContext::tfcMode;
        if (comment.starts_with("debug map menu")) return UHI::ActivationContext::mapDebug;
        if (comment == "lockpicking") return UHI::ActivationContext::lockpicking;
        if (comment == "creations menu" || comment == "marketplace") return UHI::ActivationContext::marketplace;
        if (comment == "favor") return UHI::ActivationContext::favor;
        return std::nullopt;
    }

    bool HiddenGameControlContext(const UHI::ActivationContext context) noexcept
    {
        return context == UHI::ActivationContext::console ||
            context == UHI::ActivationContext::debugText ||
            context == UHI::ActivationContext::debugOverlay ||
            context == UHI::ActivationContext::mapDebug ||
            context == UHI::ActivationContext::marketplace;
    }

    bool HiddenGameControlAction(std::string action)
    {
        std::ranges::transform(action, action.begin(), [](const unsigned char ch) {
            return static_cast<char>(std::tolower(ch));
        });
        return action.find("console") != std::string::npos ||
            action.find("debug") != std::string::npos ||
            action.find("creation") != std::string::npos ||
            action.find("marketplace") != std::string::npos;
    }
}

namespace UHI::Scanners
{
    std::vector<HotkeyRecord> ControlMapScanner::Scan(const std::filesystem::path& path) const
    {
        std::ifstream input(path);
        if (!input) return {};
        const std::string content((std::istreambuf_iterator<char>(input)), {});
        return ScanContent(path, content);
    }

    std::vector<HotkeyRecord> ControlMapScanner::ScanContent(const std::filesystem::path& source,
        const std::string_view content) const
    {
        std::vector<HotkeyRecord> records;
        std::istringstream input{ std::string(content) };
        auto currentContext = ActivationContext::gameplay;

        std::string line;
        std::size_t lineNumber{};
        while (std::getline(input, line)) {
            ++lineNumber;
            const auto trimmed = Trim(line);
            if (trimmed.starts_with("//") || trimmed.starts_with('#')) {
                if (const auto context = ContextFromComment(trimmed)) currentContext = *context;
                continue;
            }
            if (trimmed.empty()) continue;
            if (HiddenGameControlContext(currentContext)) continue;

            // Bethesda's file uses a variable number of tabs merely for visual
            // alignment. Parse the three code tokens relative to the trailing
            // remap flags instead of treating every tab as a field separator.
            std::string action;
            std::array<std::string, 3> inputFields;
            const auto tabColumns = SplitTabColumns(line);
            if (line.find('\t') != std::string::npos && tabColumns.size() >= 7U) {
                action = tabColumns[0];
                std::copy_n(tabColumns.begin() + 1, inputFields.size(), inputFields.begin());
            } else {
                static const std::regex row(
                    R"(^\s*(.+?)\s+(\S+)\s+(\S+)\s+(\S+)\s+([01])\s+([01])\s+([01])(?:\s+(\S+))?\s*$)");
                std::smatch match;
                if (!std::regex_match(line, match, row)) continue;
                action = Trim(match[1].str());
                for (std::size_t i = 0; i < inputFields.size(); ++i) {
                    inputFields[i] = Trim(match[2 + i].str());
                }
            }
            if (action.empty() || HiddenGameControlAction(action)) continue;

            constexpr std::array devices{ "keyboard", "mouse", "gamepad" };
            for (std::size_t deviceIndex = 0; deviceIndex < devices.size(); ++deviceIndex) {
                const auto device = devices[deviceIndex];
                auto field = inputFields[deviceIndex];
                if (Disabled(field)) continue;
                // A leading !N token is Bethesda's conditional event-link
                // syntax (for example !0,Favorites,!0,Tween Menu,!0,Pause),
                // not a physical input assignment. Do not emit its following
                // event names as candidate keys; other device columns on the
                // same row are still parsed normally.
                if (!field.empty() && field.front() == '!') continue;

                // A comma separates alternative bindings; '+' is a chord. Keep
                // one record per alternative so conflicts and the device view can
                // represent both without mistaking the comma for a chord.
                for (const auto& alternative : Split(field, ',')) {
                    if (Disabled(alternative)) continue;
                    const auto parsed = ParseExpression(alternative, device);
                    if (parsed.binding.empty()) continue;
                    records.push_back({
                        .owner = "Skyrim Controls",
                        .action = action,
                        .binding = parsed.binding,
                        .rawBinding = field,
                        .codeSystem = parsed.codeSystem,
                        .device = device,
                        .detector = "ControlMapScanner",
                        .confidence = parsed.conflictEligible ? Confidence::confirmed : Confidence::candidate,
                        .evidencePath = source,
                        .evidenceLine = lineNumber,
                        .stage = ScanStage::configuration,
                        .editable = true,
                        .conflictEligible = parsed.conflictEligible,
                        .contextMask = static_cast<std::uint32_t>(currentContext),
                        .contextConfidence = ContextConfidence::confirmed
                    });
                }
            }
        }
        return records;
    }

    std::vector<HotkeyRecord> ControlMapScanner::KnownGameDefaults(
        const std::filesystem::path& gameRoot) const
    {
        struct Row
        {
            std::string_view action;
            std::string_view keyboard;
            std::string_view mouse;
            std::string_view gamepad;
            std::size_t line;
        };
        static constexpr std::array<Row, 42> rows{{
            { "Forward", "0x11", "0xff", "0xff", 15 },
            { "Back", "0x1f", "0xff", "0xff", 16 },
            { "Strafe Left", "0x1e", "0xff", "0xff", 17 },
            { "Strafe Right", "0x20", "0xff", "0xff", 18 },
            { "Move", "0xff", "0xff", "0x000b", 19 },
            { "Look", "0xff", "0xa", "0x000c", 20 },
            { "Left Attack/Block", "0xff", "0x1", "0x0009", 21 },
            { "Right Attack/Block", "0xff", "0x0", "0x000a", 22 },
            { "Activate", "0x12", "0xff", "0x1000", 23 },
            { "Ready Weapon", "0x13", "0xff", "0x4000", 24 },
            { "Tween Menu", "0x0f", "0xff", "0x2000", 25 },
            { "Toggle POV", "0x21", "0xff", "0x0080", 26 },
            { "Zoom Out", "0xff", "0x9", "0xff", 27 },
            { "Zoom In", "0xff", "0x8", "0xff", 28 },
            { "Jump", "0x39", "0xff", "0x8000", 29 },
            { "Sprint", "0x38", "0xff", "0x0100", 30 },
            { "Shout", "0x2c", "0xff", "0x0200", 31 },
            { "Sneak", "0x1d", "0xff", "0x0040", 32 },
            { "Run", "0x2a", "0xff", "0xff", 33 },
            { "Toggle Always Run", "0x3a", "0xff", "0xff", 34 },
            { "Auto-Move", "0x2e", "0xff", "0xff", 35 },
            { "Favorites", "0x10", "0xff", "0x0001,0x0002", 36 },
            { "Hotkey1", "0x02,0x4f", "0xff", "0x0004", 37 },
            { "Hotkey2", "0x03,0x50", "0xff", "0x0008", 38 },
            { "Hotkey3", "0x04,0x51", "0xff", "0xff", 39 },
            { "Hotkey4", "0x05,0x4b", "0xff", "0xff", 40 },
            { "Hotkey5", "0x06,0x4c", "0xff", "0xff", 41 },
            { "Hotkey6", "0x07,0x4d", "0xff", "0xff", 42 },
            { "Hotkey7", "0x08,0x47", "0xff", "0xff", 43 },
            { "Hotkey8", "0x09,0x48", "0xff", "0xff", 44 },
            { "Quicksave", "0x3f", "0xff", "0xff", 45 },
            { "Quickload", "0x43", "0xff", "0xff", 46 },
            { "Wait", "0x14", "0xff", "0x0020", 47 },
            { "Journal", "0x24", "0xff", "0x0010", 48 },
            { "Pause", "0x1", "0xff", "0xff", 49 },
            { "Screenshot", "0xb7", "0xff", "0xff", 50 },
            { "Multi-Screenshot", "0x1d+0xb7,0x9d+0xb7", "0xff", "0xff", 51 },
            { "CameraPath", "0x58", "0xff", "0xff", 53 },
            { "Quick Inventory", "0x17", "0xff", "0xff", 54 },
            { "Quick Magic", "0x19", "0xff", "0xff", 55 },
            { "Quick Stats", "0x35", "0xff", "0xff", 56 },
            { "Quick Map", "0x32", "0xff", "0xff", 57 }
        }};

        // This is a logical loose-override path only. The built-in default
        // table never opens Skyrim - Interface.bsa.
        const auto looseSource = gameRoot / "Data" / "Interface" / "Controls" / "PC" / "controlmap.txt";
        std::vector<HotkeyRecord> records;
        constexpr std::array devices{ "keyboard", "mouse", "gamepad" };
        for (const auto& known : rows) {
            const std::array fields{ known.keyboard, known.mouse, known.gamepad };
            for (std::size_t deviceIndex = 0; deviceIndex < devices.size(); ++deviceIndex) {
                if (Disabled(std::string(fields[deviceIndex]))) continue;
                for (const auto& alternative : Split(fields[deviceIndex], ',')) {
                    const auto parsed = ParseExpression(alternative, devices[deviceIndex]);
                    if (parsed.binding.empty()) continue;
                    records.push_back({
                        .owner = "Skyrim Controls",
                        .action = std::string(known.action),
                        .binding = parsed.binding,
                        .rawBinding = std::string(fields[deviceIndex]),
                        .codeSystem = parsed.codeSystem,
                        .device = devices[deviceIndex],
                        .detector = "ControlMapScanner",
                        .confidence = Confidence::confirmed,
                        .evidencePath = looseSource,
                        .evidenceLine = known.line,
                        .stage = ScanStage::configuration,
                        .editable = false,
                        .conflictEligible = parsed.conflictEligible,
                        .contextMask = static_cast<std::uint32_t>(ActivationContext::gameplay),
                        .contextConfidence = ContextConfidence::confirmed
                    });
                }
            }
        }
        return records;
    }
}
