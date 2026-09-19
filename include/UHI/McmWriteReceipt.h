#pragma once

#include <atomic>
#include <cstdint>
#include <optional>

namespace UHI
{
    struct McmWriteReceipt
    {
        enum class Outcome { waiting, applied, rejected, timedOut };
        std::atomic_bool handlerCompleted{ false };
        std::uint64_t deadline{};

        Outcome Inspect(std::uint64_t now, std::optional<std::int32_t> actual, std::int32_t requested) const
        {
            if (handlerCompleted.load()) return actual && *actual == requested ? Outcome::applied : Outcome::rejected;
            return now >= deadline ? Outcome::timedOut : Outcome::waiting;
        }
    };
}
