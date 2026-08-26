#pragma once

#include "UHI/HotkeyRecord.h"

#include <string_view>

namespace UHI
{
    enum class HotkeyCategory
    {
        all,
        game,
        environment,
        character,
        combat,
        sexlab,
        user_interface,
        external
    };

    HotkeyCategory ClassifyHotkey(const HotkeyRecord& record);
    std::string_view HotkeyCategoryName(HotkeyCategory category);
}
