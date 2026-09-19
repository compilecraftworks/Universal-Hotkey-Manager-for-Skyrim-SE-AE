#pragma once

namespace UHI
{
    template <class Constants, class Message>
    constexpr bool BeginsGameTransition(Message message) noexcept
    {
        // kDeleteGame is deletion of a save file, not a VM/load transition.
        return message == Constants::kPreLoadGame;
    }
}
