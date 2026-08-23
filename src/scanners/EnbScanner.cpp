#include "UHI/scanners/EnbScanner.h"
#include "UHI/ConfigBindingParser.h"

#include <algorithm>
#include <charconv>
#include <cctype>
#include <fstream>
#include <string>
#include <unordered_map>

namespace
{
    std::string Trim(std::string value)
    {
        const auto space = [](const unsigned char ch) { return std::isspace(ch) != 0; };
        value.erase(value.begin(), std::find_if_not(value.begin(), value.end(), space));
        value.erase(std::find_if_not(value.rbegin(), value.rend(), space).base(), value.end());
        return value;
    }

    bool ParseDecimal(const std::string_view text, std::uint32_t& value)
    {
        const auto [last, error] = std::from_chars(text.data(), text.data() + text.size(), value, 10);
        return error == std::errc{} && last == text.data() + text.size();
    }

    bool RequiresCombination(const std::string_view key)
    {
        return key != "KeyReadConfig" && key != "KeyShowFPS" && key != "KeyScreenshot";
    }
}

namespace UHI::Scanners
{
    std::vector<HotkeyRecord> EnbScanner::Scan(const std::filesystem::path& iniPath) const
    {
        std::ifstream input(iniPath);
        if (!input) return {};
        struct Entry { std::string key; std::uint32_t value{}; std::size_t line{}; };
        std::vector<Entry> entries;
        std::uint32_t combination{};
        bool inputSection{};
        std::string line;
        std::size_t lineNumber{};
        while (std::getline(input, line)) {
            ++lineNumber;
            auto text = Trim(line);
            if (text.empty() || text.starts_with("//") || text.starts_with(';') || text.starts_with('#')) continue;
            if (text.front() == '[' && text.back() == ']') {
                auto section = text.substr(1, text.size() - 2);
                std::ranges::transform(section, section.begin(), [](const unsigned char ch) { return static_cast<char>(std::toupper(ch)); });
                inputSection = section == "INPUT";
                continue;
            }
            if (!inputSection) continue;
            const auto equals = text.find('=');
            if (equals == std::string::npos) continue;
            auto key = Trim(text.substr(0, equals));
            auto raw = Trim(text.substr(equals + 1));
            std::uint32_t value{};
            if (!key.starts_with("Key") || !ParseDecimal(raw, value) || value == 0 || value > 255) continue;
            if (key == "KeyCombination") combination = value;
            else entries.push_back({ std::move(key), value, lineNumber });
        }

        static const std::unordered_map<std::string, std::string> actions{
            { "KeyReadConfig", "Reload ENB configuration" }, { "KeyUseEffect", "Toggle ENB effects" },
            { "KeyFPSLimit", "Toggle ENB FPS limiter" }, { "KeyShowFPS", "Toggle ENB FPS display" },
            { "KeyScreenshot", "Take ENB screenshot" }, { "KeyEditor", "Open ENB editor" },
            { "KeyFreeVRAM", "Free video memory" }, { "KeyBruteForce", "Toggle brute force mode" },
            { "KeyDepthOfField", "Toggle depth of field" }, { "KeyDof", "Toggle depth of field" },
            { "KeyBloom", "Toggle bloom" }, { "KeyOcclusion", "Toggle ambient occlusion" },
            { "KeyReflection", "Toggle reflections" }, { "KeyShadow", "Toggle shadows" },
            { "KeyWater", "Toggle water effects" }
        };

        std::vector<HotkeyRecord> records;
        for (const auto& entry : entries) {
            const auto found = actions.find(entry.key);
            if (found == actions.end()) continue;
            auto key = ParseVirtualKeyCode(entry.value);
            if (key.binding.empty()) continue;
            std::string binding;
            bool eligible = key.conflictEligible;
            if (combination != 0 && RequiresCombination(entry.key)) {
                const auto modifier = ParseVirtualKeyCode(combination);
                binding = modifier.binding + '+';
                eligible = eligible && !modifier.binding.empty();
            }
            binding += key.binding;
            records.push_back({
                .owner = "ENBSeries", .action = found->second, .binding = std::move(binding),
                .rawBinding = std::to_string(entry.value), .settingName = entry.key,
                .settingSection = "INPUT", .codeSystem = "ENB decimal Windows virtual-key code",
                .device = key.device, .detector = "EnbScanner",
                .confidence = eligible ? Confidence::confirmed : Confidence::candidate,
                .evidencePath = iniPath, .evidenceLine = entry.line, .stage = ScanStage::configuration,
                .editable = true,
                .conflictEligible = eligible
            });
        }
        return records;
    }
}
