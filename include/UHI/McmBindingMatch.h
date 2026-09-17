#pragma once
#include <algorithm>
#include <cctype>
#include <string>
#include <string_view>

namespace UHI
{
    // A custom getter named KeyUp may wrap <Owner>KeyUp. Only strip a
    // proven owner prefix, never an arbitrary substring from another mod.
    inline bool MatchesOwnerPrefixedSetting(std::string_view setting,
        std::string_view candidate, std::string_view owner)
    {
        const auto canonical = [](std::string_view text) {
            std::string result;
            for (unsigned char c : text) if (std::isalnum(c)) result += static_cast<char>(std::tolower(c));
            return result;
        };
        const auto target = canonical(setting), actual = canonical(candidate), mod = canonical(owner);
        if (target.size() < 4U || actual.size() <= target.size() || !actual.ends_with(target)) return false;
        const auto prefix = actual.substr(0, actual.size() - target.size());
        return prefix.size() >= 3U && mod.starts_with(prefix);
    }
}
