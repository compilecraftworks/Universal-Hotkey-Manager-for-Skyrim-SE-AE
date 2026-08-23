#pragma once

#include "UHI/HotkeyRecord.h"

#include <string_view>

namespace UHI
{
    enum class ContextEvidenceSource
    {
        structuredConfiguration,
        papyrusFunction,
        nativeBinary
    };

    struct ContextInference
    {
        std::uint32_t mask{};
        ContextConfidence confidence{ ContextConfidence::unknown };
    };

    // Conservative static inference. Absence of a restriction is never treated
    // as proof of global activation.
    [[nodiscard]] ContextInference InferActivationContext(
        std::string_view evidence, ContextEvidenceSource source);

    [[nodiscard]] bool IsUiLocalInput(std::string_view owner, std::string_view action,
        std::string_view detector, std::uint32_t contextMask) noexcept;
}
