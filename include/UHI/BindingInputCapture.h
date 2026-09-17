#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string_view>

namespace UHI
{
    // Receives every button in an input batch, including held/released events.
    // Holding a candidate modifier never ends processing of the batch.
    struct CapturedInput
    {
        std::string_view device;
        std::uint32_t code{};
        std::string_view modifierDevice;
        std::uint32_t modifier{};
        bool multipleModifiers{};
    };

    class BindingInputCapture
    {
    public:
        static constexpr std::array<std::uint32_t, 6> keyboardModifiers{
            0x1DU, 0x9DU, 0x2AU, 0x36U, 0x38U, 0xB8U };

        void ObserveKeyboard(std::uint32_t code, bool pressed) noexcept
        {
            for (std::size_t i = 0; i < keyboardModifiers.size(); ++i)
                if (code == keyboardModifiers[i]) held_[i] = pressed;
        }

        void ResetCandidates() noexcept { mouse_ = gamepad_ = 0; }

        [[nodiscard]] std::optional<CapturedInput> Process(std::string_view device,
            std::uint32_t code, bool down, bool up) noexcept
        {
            std::uint32_t modifier{};
            unsigned count{};
            for (std::size_t i = 0; i < held_.size(); ++i) {
                if (held_[i]) { modifier = keyboardModifiers[i]; ++count; }
            }
            if (device == "keyboard") {
                for (const auto candidate : keyboardModifiers)
                    if (code == candidate) {
                        if (up) return CapturedInput{device, code};
                        return std::nullopt;
                    }
            }
            auto* candidate = device == "mouse" ? &mouse_ : device == "gamepad" ? &gamepad_ : nullptr;
            const bool isCandidate = device == "mouse" ? (code == 3U || code == 4U) :
                device == "gamepad" && (code == 271U || code == 274U || code == 275U || code == 280U || code == 281U);
            if (candidate && isCandidate && modifier == 0U) {
                if (down && *candidate == 0U) { *candidate = code; return std::nullopt; }
                if (up && *candidate == code) { *candidate = 0U; return CapturedInput{device, code}; }
            }
            if (!down) return std::nullopt;
            CapturedInput result{device, code, modifier ? "keyboard" : "", modifier, count > 1U};
            if (candidate && *candidate != 0U) {
                result.multipleModifiers = result.multipleModifiers || modifier != 0U;
                result.modifierDevice = device;
                result.modifier = *candidate;
                *candidate = 0U;
            }
            return result;
        }

    private:
        std::array<bool, 6> held_{};
        std::uint32_t mouse_{};
        std::uint32_t gamepad_{};
    };
}
