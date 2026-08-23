#include "UHI/scanners/CommunityShadersScanner.h"
#include "UHI/ConfigBindingParser.h"
#include "UHI/InputCodeFormatter.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <fstream>
#include <regex>
#include <string>
#include <unordered_map>

namespace
{
    struct DecodedInput
    {
        std::string binding;
        std::string device{ "unknown" };
        bool eligible{};
        bool modifier{};
    };

    DecodedInput Decode(const std::uint32_t packed)
    {
        const auto device = packed < 0x10000 ? 3U : packed >> 16U;
        const auto key = packed < 0x10000 ? packed : packed & 0xFFFFU;
        if (device == 3) {
            auto parsed = UHI::ParseVirtualKeyCode(key);
            const bool modifier = key == 0x10 || key == 0x11 || key == 0x12 ||
                (key >= 0xA0 && key <= 0xA5);
            return { std::move(parsed.binding), "keyboard", parsed.conflictEligible || modifier, modifier };
        }
        if (device == 4) {
            auto parsed = UHI::ParseVirtualKeyCode(key);
            return { std::move(parsed.binding), "mouse", parsed.device == "mouse" && parsed.conflictEligible, false };
        }
        if (device == 5) {
            auto binding = UHI::FormatControlMapGamepadCode(key);
            const bool known = !binding.empty();
            return { binding.empty() ? "Gamepad " + std::to_string(key) + " (unknown)" : std::move(binding),
                "gamepad", known, false };
        }
        return { "Packed input " + std::to_string(packed) + " (unknown device)", "unknown", false, false };
    }

    std::vector<std::uint32_t> ParseValues(const std::string_view raw)
    {
        std::vector<std::uint32_t> values;
        static const std::regex number(R"(\d+)");
        const std::string text(raw);
        for (std::sregex_iterator it(text.begin(), text.end(), number), end; it != end; ++it) {
            std::uint32_t value{};
            const auto token = it->str();
            const auto [last, error] = std::from_chars(token.data(), token.data() + token.size(), value);
            if (error == std::errc{} && last == token.data() + token.size() && value != 0) values.push_back(value);
        }
        return values;
    }

    std::size_t LineAt(const std::string_view text, const std::size_t offset)
    {
        return 1 + static_cast<std::size_t>(std::count(text.begin(), text.begin() +
            static_cast<std::ptrdiff_t>(std::min(offset, text.size())), '\n'));
    }
}

namespace UHI::Scanners
{
    std::vector<HotkeyRecord> CommunityShadersScanner::Scan(const std::filesystem::path& settingsPath) const
    {
        std::ifstream input(settingsPath, std::ios::binary);
        if (!input) return {};
        const std::string content((std::istreambuf_iterator<char>(input)), {});
        static const std::unordered_map<std::string, std::string> actions{
            { "ToggleKey", "Open Community Shaders menu" },
            { "SkipCompilationKey", "Skip shader compilation" },
            { "EffectToggleKey", "Toggle all effects" },
            { "OverlayToggleKey", "Toggle overlays" },
            { "ShaderBlockPrevKey", "Previous blocked shader" },
            { "ShaderBlockNextKey", "Next blocked shader" },
            { "CSEditorToggleKey", "Toggle CS editor" },
            { "ScreenshotKey", "Take screenshot" },
            { "Effects11ToggleKey", "Toggle Effects 11" }
        };

        std::vector<HotkeyRecord> records;
        // Current CS saves a single keyboard VK as an integer, and a chord or
        // device-qualified input as an array/packed integer. This also reads the
        // legacy scalar SettingsUser.json format.
        static const std::regex member(R"UHI("([A-Za-z0-9]+Key)"\s*:\s*(\[[^\]]*\]|\d+))UHI");
        for (std::sregex_iterator it(content.begin(), content.end(), member), end; it != end; ++it) {
            const auto found = actions.find((*it)[1].str());
            if (found == actions.end()) continue;
            const auto raw = (*it)[2].str();
            const auto values = ParseValues(raw);
            if (values.empty()) continue;

            std::string binding;
            std::string device = "keyboard";
            bool eligible = true;
            bool onlyModifiers = true;
            for (const auto value : values) {
                auto decoded = Decode(value);
                if (!binding.empty()) binding += '+';
                binding += decoded.binding;
                device = decoded.device;
                eligible = eligible && decoded.eligible;
                onlyModifiers = onlyModifiers && decoded.modifier;
            }
            eligible = eligible && !onlyModifiers;
            records.push_back({
                .owner = "Community Shaders",
                .action = found->second,
                .binding = std::move(binding),
                .rawBinding = raw,
                .settingName = (*it)[1].str(),
                .codeSystem = "Community Shaders InputCombo (Windows VK / packed device)",
                .device = std::move(device),
                .detector = "CommunityShadersScanner",
                .confidence = eligible ? Confidence::confirmed : Confidence::candidate,
                .evidencePath = settingsPath,
                .evidenceLine = LineAt(content, static_cast<std::size_t>(it->position())),
                .stage = ScanStage::configuration,
                .editable = true,
                .conflictEligible = eligible
            });
        }
        return records;
    }
}
