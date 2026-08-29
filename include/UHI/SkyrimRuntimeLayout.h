#pragma once

#include <cstdint>
#include <optional>

namespace UHI
{
    inline constexpr std::uint32_t kLegacyFavorContextIndex = 16;
    inline constexpr std::uint32_t kCanonicalMarketplaceContextIndex = 16;
    inline constexpr std::uint32_t kCanonicalFavorContextIndex = 17;

    struct SkyrimInputContextLayout
    {
        std::uint32_t runtimeContextCount{ 0 };
        bool hasMarketplaceContext{ false };

        [[nodiscard]] constexpr std::uint32_t CanonicalContextIndex(
            const std::uint32_t runtimeIndex) const noexcept
        {
            if (!hasMarketplaceContext && runtimeIndex == kLegacyFavorContextIndex) {
                return kCanonicalFavorContextIndex;
            }
            return runtimeIndex;
        }
    };

    struct NativeRendererHookLayout
    {
        std::uint64_t d3dInitRelocationID{ 0 };
        std::uintptr_t d3dInitCallOffset{ 0 };
    };

    [[nodiscard]] constexpr std::optional<NativeRendererHookLayout>
        NativeRendererHookLayoutForVersion(
        const std::uint16_t major, const std::uint16_t minor,
        const std::uint16_t patch, const std::uint16_t build) noexcept
    {
        if (major != 1 || build != 0) return std::nullopt;
        if (minor == 5 && patch == 97) {
            return NativeRendererHookLayout{ 75595, 0x50 };
        }
        if (minor != 6) return std::nullopt;
        switch (patch) {
        case 317:
        case 318:
        case 323:
        case 342:
        case 353:
        case 629:
        case 640:
        case 659:
        case 678:
        case 1130:
        case 1170:
        case 1179:
            return NativeRendererHookLayout{ 77226, 0x2BC };
        default:
            return std::nullopt;
        }
    }

    [[nodiscard]] constexpr std::optional<SkyrimInputContextLayout>
        SkyrimInputContextLayoutForVersion(
        const std::uint16_t major, const std::uint16_t minor,
        const std::uint16_t patch) noexcept
    {
        // The verified flat SE/AE families are Skyrim 1.5 and 1.6.  Do not
        // guess a future layout: using a wrong count reads the following
        // ControlMap runtime data as an InputContext pointer.
        if (major != 1 || (minor != 5 && minor != 6)) return std::nullopt;

        const bool hasMarketplace = minor == 6 && patch >= 1130;
        return SkyrimInputContextLayout{
            .runtimeContextCount = hasMarketplace ? 18U : 17U,
            .hasMarketplaceContext = hasMarketplace
        };
    }
}
