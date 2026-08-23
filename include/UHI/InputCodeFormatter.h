#pragma once

#include <string>
#include <string_view>
#include <cstdint>

namespace UHI
{
    // These code spaces deliberately stay separate. The same integer means a
    // different physical input in DirectInput, Win32 VK, SKSE and controlmap.
    std::string FormatDirectInputScanCode(std::uint32_t code);
    std::string FormatSkseMouseCode(std::uint32_t code);
    std::string FormatSkseGamepadCode(std::uint32_t code);
    std::string FormatControlMapMouseCode(std::uint32_t code);
    std::string FormatControlMapGamepadCode(std::uint32_t code);
}
