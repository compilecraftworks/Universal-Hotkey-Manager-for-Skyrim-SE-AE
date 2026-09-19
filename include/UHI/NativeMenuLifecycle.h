#pragma once

#include <atomic>
#include <cstdint>

namespace UHI
{
    // One authority for queued visibility and the engine's actual menu state.
    // Rendering never changes this state. Revisions cancel stale queued work.
    class NativeMenuLifecycle
    {
    public:
        enum class Change { none, shown, hidden };
        using Ticket = std::uint64_t;

        Ticket Request(bool open) noexcept
        {
            auto previous = state_.load();
            Ticket next;
            do {
                next = ((previous + 4U) & ~kRequested) | (open ? kRequested : 0U);
            } while (!state_.compare_exchange_weak(previous, next));
            return next & ~kFlags;
        }

        // The engine's hide/force-hide/destruction is authoritative, including
        // when it interrupts an opening request or a binding-capture popup.
        void Closed() noexcept
        {
            auto previous = state_.load();
            while ((previous & kFlags) != 0U &&
                !state_.compare_exchange_weak(previous, (previous + 4U) & ~kFlags)) {}
        }

        template <class Message>
        Change ProcessMessage(Message message) noexcept
        {
            if (message == Message::kHide || message == Message::kForceHide) {
                Closed();
                return Change::hidden;
            }
            if (message == Message::kShow || message == Message::kReshow) {
                state_.fetch_or(kVisible);
                return Change::shown;
            }
            return Change::none;
        }

        bool IsOpen() const noexcept { return (state_.load() & kFlags) != 0U; }
        bool WantsOpen() const noexcept { return (state_.load() & kRequested) != 0U; }
        bool CanRender() const noexcept { return (state_.load() & kFlags) == kFlags; }
        Ticket Revision() const noexcept { return state_.load() & ~kFlags; }
        bool IsCurrent(Ticket ticket) const noexcept { return Revision() == ticket; }

    private:
        static constexpr Ticket kRequested = 1U;
        static constexpr Ticket kVisible = 2U;
        static constexpr Ticket kFlags = kRequested | kVisible;
        std::atomic<Ticket> state_{};
    };
}
