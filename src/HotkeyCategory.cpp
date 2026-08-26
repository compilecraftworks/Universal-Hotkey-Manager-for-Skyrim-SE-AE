#include "UHI/HotkeyCategory.h"

#include <algorithm>
#include <cctype>
#include <initializer_list>
#include <string>

namespace
{
    std::string SearchText(std::string value)
    {
        std::string result;
        result.reserve(value.size() * 2);
        unsigned char previous{};
        for (const unsigned char ch : value) {
            if (std::isupper(ch) && previous != 0 && std::islower(previous)) result.push_back(' ');
            result.push_back(std::isalnum(ch) ? static_cast<char>(std::tolower(ch)) : ' ');
            previous = ch;
        }
        std::string compact;
        compact.reserve(result.size() + 2);
        compact.push_back(' ');
        bool spaced = true;
        for (const char ch : result) {
            if (ch == ' ') {
                if (!spaced) compact.push_back(' ');
                spaced = true;
            } else {
                compact.push_back(ch);
                spaced = false;
            }
        }
        if (!spaced) compact.push_back(' ');
        return compact;
    }

    bool Contains(const std::string& value, const std::string_view needle)
    {
        return value.find(needle) != std::string::npos;
    }

    bool ContainsAny(const std::string& value,
        const std::initializer_list<std::string_view> needles)
    {
        return std::ranges::any_of(needles, [&](const auto needle) { return Contains(value, needle); });
    }

    int Score(const std::string& value,
        const std::initializer_list<std::pair<std::string_view, int>> signals)
    {
        int score{};
        for (const auto& [signal, weight] : signals) {
            if (Contains(value, signal)) score += weight;
        }
        return score;
    }

    UHI::HotkeyCategory ExplicitCategory(const std::string& hint)
    {
        if (hint == " game ") return UHI::HotkeyCategory::game;
        if (hint == " environment ") return UHI::HotkeyCategory::environment;
        if (hint == " character ") return UHI::HotkeyCategory::character;
        if (hint == " combat ") return UHI::HotkeyCategory::combat;
        if (hint == " sexlab ") return UHI::HotkeyCategory::sexlab;
        if (hint == " interface ") return UHI::HotkeyCategory::user_interface;
        if (hint == " external " || hint == " external tools ") return UHI::HotkeyCategory::external;
        return UHI::HotkeyCategory::all;
    }
}

namespace UHI
{
    HotkeyCategory ClassifyHotkey(const HotkeyRecord& record)
    {
        // Both Bethesda's compiled default table and a loose virtual
        // Data/Interface/Controls/PC/controlmap.txt are game controls. Do not
        // scatter them into mod-purpose categories based on words such as
        // "Attack", "Inventory" or "Camera" in the action label.
        if (record.detector == "ControlMapScanner") {
            return HotkeyCategory::game;
        }
        const auto owner = SearchText(record.owner);
        const auto action = SearchText(record.action);
        const auto combined = owner + action;

        // Framework identity is stronger than generic action words such as
        // "menu", "equipment" or "body". Resolve known ecosystems first.
        if (ContainsAny(owner, { " skyui ", " sky ui ", " morehud ", " more hud ",
                " skse menu framework ", " modex ", " dmenu ", " wheeler ",
                " consoleplusplus ", " console plus plus " })) return HotkeyCategory::user_interface;
        if (ContainsAny(owner, { " obody ", " schlongs of skyrim ", " sos ",
                " erin race ", " skyrim fitting system ", " skyrim vanity system ",
                " skyrim transmog ", " dynamic armor variants " })) return HotkeyCategory::character;
        if (ContainsAny(owner, { " private needs ", " devious devices ", " dd ", " sex dispatcher " })) {
            return HotkeyCategory::sexlab;
        }

        const auto hinted = ExplicitCategory(SearchText(record.categoryHint));
        if (hinted != HotkeyCategory::all) return hinted;

        if (Contains(combined, "reshade") || Contains(combined, "re shade") || Contains(combined, "community shader") ||
            Contains(combined, "enb") || Contains(combined, "overlay")) {
            return HotkeyCategory::external;
        }
        if (Contains(combined, "sexlab") || Contains(combined, "sex lab")) {
            return HotkeyCategory::sexlab;
        }
        if (owner == " skyrim " || owner == " skyrim se " || owner == " game ") {
            return HotkeyCategory::game;
        }

        // The action name is stronger evidence than the owner. This matters for
        // names such as "Open Target Lock Menu": it is a combat feature even
        // though the word "menu" also occurs.
        const auto scoreCategory = [&](const std::initializer_list<std::pair<std::string_view, int>> actionSignals,
                                       const std::initializer_list<std::pair<std::string_view, int>> ownerSignals) {
            return Score(action, actionSignals) + Score(owner, ownerSignals);
        };
        const int combat = scoreCategory({
            { " combat ", 8 }, { " attack ", 7 }, { " power attack ", 9 }, { " block ", 7 },
            { " parry ", 9 }, { " dodge ", 9 }, { " evade ", 8 }, { " evasion ", 8 },
            { " guard ", 6 }, { " bash ", 7 }, { " target lock ", 10 }, { " lock on ", 9 },
            { " stance ", 8 }, { " weapon ", 5 }, { " moveset ", 8 }, { " archery ", 8 },
            { " melee ", 7 }, { " bow ", 6 }, { " crossbow ", 7 }, { " shield ", 6 },
            { " counter ", 7 }, { " recoil ", 6 }, { " stagger ", 6 }, { " poise ", 7 },
            { " kill move ", 8 }, { " finisher ", 8 }, { " execution ", 7 }, { " combo ", 8 },
            { " dual wield ", 8 }, { " timed block ", 9 }, { " aim ", 5 }, { " shout ", 6 }
        }, {
            { " combat ", 5 }, { " parry ", 6 }, { " dodge ", 6 }, { " archery ", 6 },
            { " stance ", 6 }, { " valhalla ", 7 }, { " precision ", 6 }, { " wildcat ", 6 },
            { " smilodon ", 6 }, { " blade and blunt ", 7 }, { " true directional movement ", 6 },
            { " dual wield parrying ", 8 }, { " bow rapid combo ", 8 }, { " bfco ", 7 },
            { " mco ", 6 }, { " dmco ", 7 }, { " tudm ", 7 }, { " ocpa ", 7 }, { " scar ", 6 }
        });
        const int character = scoreCategory({
            { " character ", 7 }, { " actor ", 5 }, { " inventory ", 6 }, { " equipment ", 8 },
            { " equip ", 6 }, { " unequip ", 6 }, { " outfit ", 8 }, { " armor ", 6 },
            { " follower ", 8 }, { " companion ", 7 }, { " appearance ", 7 }, { " body ", 5 },
            { " face ", 5 }, { " hair ", 5 }, { " pose ", 5 }, { " expression ", 6 },
            { " spell ", 7 }, { " magic ", 7 }, { " potion ", 6 }, { " perk ", 6 },
            { " skill ", 5 }, { " transform ", 7 }, { " favorite ", 5 }
        }, {
            { " character ", 5 }, { " equipment ", 6 }, { " outfit ", 6 },
            { " follower ", 7 }, { " companion ", 6 }, { " appearance ", 6 }, { " racemenu ", 6 }
        });
        const int environment = scoreCategory({
            { " environment ", 7 }, { " world ", 5 }, { " camera ", 7 }, { " photo mode ", 8 },
            { " weather ", 8 }, { " time ", 4 }, { " survival ", 7 }, { " camp ", 6 },
            { " travel ", 6 }, { " horse ", 6 }, { " mount ", 5 }, { " movement ", 5 },
            { " sprint ", 5 }, { " jump ", 5 }, { " sneak ", 5 }, { " walk ", 4 },
            { " interact ", 4 }, { " activate ", 4 }, { " lantern ", 6 }, { " zoom ", 5 }
        }, {
            { " smooth cam ", 7 }, { " camera ", 5 }, { " weather ", 6 },
            { " survival ", 6 }, { " frostfall ", 7 }, { " campfire ", 7 }, { " convenient horses ", 7 }
        });
        const int interfaceScore = scoreCategory({
            { " interface ", 7 }, { " menu ", 4 }, { " hud ", 8 }, { " widget ", 8 },
            { " overlay ", 6 }, { " notification ", 7 }, { " console ", 6 }, { " search ", 4 },
            { " debug ", 5 }, { " settings ", 5 }, { " configuration ", 5 }, { " panel ", 5 },
            { " window ", 5 }, { " prompt ", 5 }, { " display ", 4 }, { " screenshot ", 6 }
        }, {
            { " interface ", 5 }, { " hud ", 7 }, { " widget ", 7 }, { " menu ", 3 },
            { " console ", 4 }, { " ui ", 3 }
        });

        // Purpose-specific categories win ties over the generic interface bucket.
        const int bestSpecific = (std::max)({ combat, character, environment });
        if (bestSpecific > 0 && bestSpecific >= interfaceScore) {
            if (combat == bestSpecific) return HotkeyCategory::combat;
            if (character == bestSpecific) return HotkeyCategory::character;
            return HotkeyCategory::environment;
        }
        if (interfaceScore > 0) return HotkeyCategory::user_interface;
        // Do not use Interface as a catch-all. Unclassified gameplay utilities
        // fit the broad Environment tab until stronger evidence is learned.
        return HotkeyCategory::environment;
    }

    std::string_view HotkeyCategoryName(const HotkeyCategory category)
    {
        switch (category) {
        case HotkeyCategory::all: return "All";
        case HotkeyCategory::game: return "Game";
        case HotkeyCategory::environment: return "Environment";
        case HotkeyCategory::character: return "Character";
        case HotkeyCategory::combat: return "Combat";
        case HotkeyCategory::sexlab: return "SexLab";
        case HotkeyCategory::user_interface: return "Interface";
        case HotkeyCategory::external: return "External tools";
        }
        return "Interface";
    }
}
