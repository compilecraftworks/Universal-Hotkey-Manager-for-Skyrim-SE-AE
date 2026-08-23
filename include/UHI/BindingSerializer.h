#pragma once

#include "UHI/HotkeyRecord.h"

#include <cstdint>
#include <string>
#include <string_view>

namespace UHI
{
    struct SerializedBinding
    {
        std::string display;
        std::string raw;
        std::string error;

        [[nodiscard]] explicit operator bool() const noexcept { return error.empty() && !raw.empty(); }
    };

    // Converts the physical input captured by the native input sink into the
    // exact encoding used by the setting that produced record. Unknown or
    // lossy device/encoding combinations are rejected instead of being saved.
    [[nodiscard]] SerializedBinding SerializeCapturedBinding(const HotkeyRecord& record,
        std::string_view mainDevice, std::uint32_t mainCode,
        std::string_view modifierDevice = {}, std::uint32_t modifierCode = 0U);
}
