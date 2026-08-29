#pragma once

#include <atomic>
#include <cstdint>

namespace UHI
{
    // Owns one physical Escape press while a child popup/capture is closing.
    // The release is acknowledged over two render boundaries so the same
    // press cannot be reused by the parent window after the popup disappears.
    class EscapeCloseGuard
    {
    public:
        enum class Phase : std::uint8_t
        {
            idle,
            childPressed,
            childReleased,
            childCooldown
        };

        void Own(const std::int64_t nowMilliseconds,
            const std::int64_t timeoutMilliseconds = 2000) noexcept
        {
            phase_.store(Phase::childPressed);
            deadlineMilliseconds_.store(nowMilliseconds + timeoutMilliseconds);
        }

        void Release() noexcept
        {
            auto expected = Phase::childPressed;
            (void)phase_.compare_exchange_strong(expected, Phase::childReleased);
        }

        [[nodiscard]] bool IsSuppressed(const std::int64_t nowMilliseconds) noexcept
        {
            const auto deadline = deadlineMilliseconds_.load();
            if (deadline <= 0 || nowMilliseconds >= deadline) {
                Reset();
                return false;
            }
            return phase_.load() != Phase::idle;
        }

        void CompleteRender(const bool escapeStillVisible) noexcept
        {
            auto expected = Phase::childReleased;
            if (phase_.compare_exchange_strong(expected, Phase::childCooldown)) return;

            expected = Phase::childCooldown;
            if (!escapeStillVisible && phase_.compare_exchange_strong(expected, Phase::idle)) {
                deadlineMilliseconds_.store(0);
            }
        }

        void Reset() noexcept
        {
            phase_.store(Phase::idle);
            deadlineMilliseconds_.store(0);
        }

        [[nodiscard]] Phase CurrentPhase() const noexcept { return phase_.load(); }

    private:
        std::atomic<Phase> phase_{ Phase::idle };
        std::atomic_int64_t deadlineMilliseconds_{ 0 };
    };
}
