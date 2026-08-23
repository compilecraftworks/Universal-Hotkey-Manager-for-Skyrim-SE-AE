#include "UHI/scanners/ReShadeScanner.h"
#include "UHI/ConfigBindingParser.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <charconv>
#include <fstream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace
{
    std::string Trim(std::string value)
    {
        const auto isSpace = [](const unsigned char ch) { return std::isspace(ch) != 0; };
        value.erase(value.begin(), std::find_if_not(value.begin(), value.end(), isSpace));
        value.erase(std::find_if_not(value.rbegin(), value.rend(), isSpace).base(), value.end());
        return value;
    }

    bool ParseReShadeTuple(const std::string_view value, std::array<unsigned int, 4>& tuple)
    {
        std::size_t start = 0;
        for (std::size_t index = 0; index < tuple.size(); ++index) {
            const auto end = value.find(',', start);
            auto part = value.substr(start, end == std::string_view::npos ? value.size() - start : end - start);
            while (!part.empty() && std::isspace(static_cast<unsigned char>(part.front()))) part.remove_prefix(1);
            while (!part.empty() && std::isspace(static_cast<unsigned char>(part.back()))) part.remove_suffix(1);
            const auto [pointer, error] = std::from_chars(part.data(), part.data() + part.size(), tuple[index]);
            if (part.empty() || error != std::errc{} || pointer != part.data() + part.size()) {
                return false;
            }
            if (index + 1 < tuple.size()) {
                if (end == std::string_view::npos) {
                    return false;
                }
                start = end + 1;
            } else if (end != std::string_view::npos) {
                return false;
            }
        }
        return true;
    }

    UHI::ParsedConfigBinding ParseReShadeBinding(const std::array<unsigned int, 4>& tuple)
    {
        if (tuple[0] == 0) return {};
        auto parsed = UHI::ParseVirtualKeyCode(tuple[0]);
        std::string prefix;
        if (tuple[1] != 0) prefix += "Ctrl+";
        if (tuple[2] != 0) prefix += "Shift+";
        if (tuple[3] != 0) prefix += "Alt+";
        parsed.binding = prefix + parsed.binding;
        parsed.codeSystem = "ReShade tuple (Windows virtual-key + modifier flags)";
        return parsed;
    }

    std::vector<unsigned int> ParseList(const std::string_view value)
    {
        std::vector<unsigned int> values;
        std::size_t start{};
        while (start <= value.size()) {
            const auto end = value.find(',', start);
            auto part = value.substr(start, end == std::string_view::npos ? value.size() - start : end - start);
            while (!part.empty() && std::isspace(static_cast<unsigned char>(part.front()))) part.remove_prefix(1);
            while (!part.empty() && std::isspace(static_cast<unsigned char>(part.back()))) part.remove_suffix(1);
            unsigned int number{};
            const auto [last, error] = std::from_chars(part.data(), part.data() + part.size(), number);
            if (part.empty() || error != std::errc{} || last != part.data() + part.size()) return {};
            values.push_back(number);
            if (end == std::string_view::npos) break;
            start = end + 1;
        }
        return values;
    }
}

namespace UHI::Scanners
{
    std::string FormatReShadeBinding(const std::string& rawBinding)
    {
        std::array<unsigned int, 4> tuple{};
        if (!ParseReShadeTuple(rawBinding, tuple) || tuple[0] == 0) {
            return {};
        }

        return ParseReShadeBinding(tuple).binding;
    }

    std::vector<HotkeyRecord> ReShadeScanner::Scan(const std::filesystem::path& iniPath) const
    {
        std::ifstream input(iniPath);
        if (!input) {
            return {};
        }

        static const std::unordered_map<std::string, std::string> knownActions{
            { "KeyOverlay", "Open overlay" },
            { "KeyEffects", "Toggle effects" },
            { "KeyPerformanceMode", "Toggle performance mode" },
            { "KeyPreviousPreset", "Previous preset" },
            { "KeyNextPreset", "Next preset" },
            { "KeyScreenshot", "Take screenshot" },
            { "KeyReload", "Reload configuration" }
            , { "KeyFPS", "Toggle FPS counter" }
            , { "KeyFrameTime", "Toggle frame-time graph" }
            , { "PresetShortcutKeys", "Preset shortcuts" }
        };

        std::vector<HotkeyRecord> results;
        std::string line;
        std::size_t lineNumber = 0;
        while (std::getline(input, line)) {
            ++lineNumber;
            const auto equals = line.find('=');
            if (equals == std::string::npos) {
                continue;
            }

            const auto key = Trim(line.substr(0, equals));
            const auto found = knownActions.find(key);
            if (found == knownActions.end()) {
                continue;
            }

            const auto value = Trim(line.substr(equals + 1));
            if (key == "PresetShortcutKeys") {
                const auto values = ParseList(value);
                for (std::size_t offset = 0, shortcut = 1; offset + 3 < values.size(); offset += 4, ++shortcut) {
                    const std::array tuple{ values[offset], values[offset + 1], values[offset + 2], values[offset + 3] };
                    const auto parsed = ParseReShadeBinding(tuple);
                    if (parsed.binding.empty()) continue;
                    results.push_back({ .owner = "ReShade", .action = "Preset shortcut " + std::to_string(shortcut),
                        .binding = parsed.binding, .rawBinding = value, .settingName = key,
                        .settingSection = "INPUT", .codeSystem = parsed.codeSystem,
                        .device = parsed.device, .detector = "ReShadeScanner", .confidence = Confidence::confirmed,
                        .evidencePath = iniPath, .evidenceLine = lineNumber, .stage = ScanStage::configuration,
                        // Every row points at one four-value slice of the same
                        // PresetShortcutKeys list. Replacing the whole raw list
                        // from one row would discard the remaining presets.
                        .editable = false, .conflictEligible = parsed.conflictEligible });
                }
                continue;
            }
            std::array<unsigned int, 4> tuple{};
            if (!ParseReShadeTuple(value, tuple)) continue;
            const auto parsed = ParseReShadeBinding(tuple);
            if (parsed.binding.empty()) {
                continue;
            }

            results.push_back({
                .owner = "ReShade",
                .action = found->second,
                .binding = parsed.binding,
                .rawBinding = value,
                .settingName = key,
                .settingSection = "INPUT",
                .codeSystem = parsed.codeSystem,
                .device = parsed.device,
                .detector = "ReShadeScanner",
                .confidence = parsed.conflictEligible ? Confidence::confirmed : Confidence::candidate,
                .evidencePath = iniPath,
                .evidenceLine = lineNumber,
                .stage = ScanStage::configuration
                , .editable = true,
                .conflictEligible = parsed.conflictEligible
            });
        }
        return results;
    }
}
