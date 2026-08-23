#include "UHI/ActivationContextInference.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <string>

namespace
{
    constexpr std::uint32_t Mask(const UHI::ActivationContext value) noexcept
    {
        return static_cast<std::uint32_t>(value);
    }

    std::string Lower(std::string_view value)
    {
        std::string result(value);
        std::ranges::transform(result, result.begin(), [](const unsigned char ch) {
            return static_cast<char>(std::tolower(ch));
        });
        return result;
    }

    bool Has(const std::string& value, const std::string_view needle)
    {
        return value.find(needle) != std::string::npos;
    }

    void AddNamedContexts(const std::string& text, std::uint32_t& mask)
    {
        static constexpr std::array contexts{
            std::pair{ UHI::ActivationContext::gameplay, std::array<std::string_view, 3>{ "gameplay", "game mode", "field mode" } },
            std::pair{ UHI::ActivationContext::menuMode, std::array<std::string_view, 3>{ "menumode", "menu mode", "any menu" } },
            std::pair{ UHI::ActivationContext::console, std::array<std::string_view, 3>{ "consolemenu", "console menu", "kconsole" } },
            std::pair{ UHI::ActivationContext::itemMenu, std::array<std::string_view, 3>{ "itemmenu", "item menu", "kitemmenu" } },
            std::pair{ UHI::ActivationContext::inventory, std::array<std::string_view, 3>{ "inventorymenu", "inventory menu", "kinventory" } },
            std::pair{ UHI::ActivationContext::favorites, std::array<std::string_view, 3>{ "favoritesmenu", "favorites menu", "kfavorites" } },
            std::pair{ UHI::ActivationContext::map, std::array<std::string_view, 3>{ "mapmenu", "map menu", "kmap" } },
            std::pair{ UHI::ActivationContext::stats, std::array<std::string_view, 3>{ "statsmenu", "magic menu", "kstats" } },
            std::pair{ UHI::ActivationContext::cursor, std::array<std::string_view, 3>{ "cursor menu", "cursor mode", "kcursor" } },
            std::pair{ UHI::ActivationContext::book, std::array<std::string_view, 3>{ "bookmenu", "book menu", "kbook" } },
            std::pair{ UHI::ActivationContext::journal, std::array<std::string_view, 3>{ "journalmenu", "journal menu", "kjournal" } },
            std::pair{ UHI::ActivationContext::lockpicking, std::array<std::string_view, 3>{ "lockpickingmenu", "lockpicking menu", "klockpicking" } },
            std::pair{ UHI::ActivationContext::debugText, std::array<std::string_view, 3>{ "debugtext", "debug text", "kdebugtext" } },
            std::pair{ UHI::ActivationContext::debugOverlay, std::array<std::string_view, 3>{ "debugoverlay", "debug overlay", "kdebugoverlay" } },
            std::pair{ UHI::ActivationContext::tfcMode, std::array<std::string_view, 3>{ "tfcmode", "tfc mode", "ktfcmode" } },
            std::pair{ UHI::ActivationContext::mapDebug, std::array<std::string_view, 3>{ "mapdebug", "map debug", "kmapdebug" } },
            std::pair{ UHI::ActivationContext::marketplace, std::array<std::string_view, 3>{ "marketplace", "creations menu", "kmarketplace" } },
            std::pair{ UHI::ActivationContext::favor, std::array<std::string_view, 3>{ "favor context", "favor mode", "kfavor" } }
        };
        for (const auto& [context, names] : contexts) {
            if (std::ranges::any_of(names, [&](const auto name) { return Has(text, name); })) mask |= Mask(context);
        }
    }
}

namespace UHI
{
    ContextInference InferActivationContext(const std::string_view evidence,
        const ContextEvidenceSource source)
    {
        if (evidence.empty()) return {};
        const auto text = Lower(evidence);
        ContextInference result;

        if (source == ContextEvidenceSource::structuredConfiguration) {
            const bool explicitScope = Has(text, "context") || Has(text, "scope") ||
                Has(text, "active in") || Has(text, "only in") || Has(text, "when menu") ||
                Has(text, "when gameplay") || Has(text, "in menu") || Has(text, "all menus") ||
                Has(text, "global") || Has(text, "always active");
            if (!explicitScope) return {};
            AddNamedContexts(text, result.mask);
            if (Has(text, "global") || Has(text, "always active") || Has(text, "all contexts")) {
                result.mask |= Mask(ActivationContext::global);
            }
            if (result.mask != 0) result.confidence = ContextConfidence::inferred;
            return result;
        }

        const bool menuApi = Has(text, "ismenuopen") || Has(text, "registerformenu") ||
            Has(text, "onmenuopen") || Has(text, "onmenuclose") || Has(text, "is in menu mode") ||
            Has(text, "isinmenumode");
        const bool engineContext = Has(text, "inputcontext") || Has(text, "input_context") ||
            Has(text, "pushinputcontext") || Has(text, "popinputcontext") || Has(text, "kmappedkey");
        if (menuApi || engineContext) AddNamedContexts(text, result.mask);
        if (menuApi && (Has(text, "isinmenumode") || Has(text, "is in menu mode")) && result.mask == 0) {
            result.mask = Mask(ActivationContext::menuMode);
        }
        if (engineContext && Has(text, "k gameplay")) result.mask |= Mask(ActivationContext::gameplay);

        // Native binaries may explicitly retain a configuration/symbol name
        // stating that their input handler is global. Merely importing a raw
        // input or Windows hotkey API is not enough proof because the callback
        // can still reject the event based on game/menu state.
        if (Has(text, "globalhotkey") || Has(text, "global hotkey") || Has(text, "allcontexts")) {
            result.mask |= Mask(ActivationContext::global);
        }
        if (result.mask != 0) result.confidence = ContextConfidence::inferred;
        return result;
    }

    bool IsUiLocalInput(const std::string_view owner, const std::string_view action,
        const std::string_view detector, const std::uint32_t contextMask) noexcept
    {
        try {
            if (detector == "ControlMapScanner" || detector == "BuiltInHotkeyTable" ||
                owner == "Skyrim default controls" || owner == "Skyrim Controls") return false;

            constexpr std::uint32_t menuContexts =
                Mask(ActivationContext::menuMode) | Mask(ActivationContext::console) |
                Mask(ActivationContext::itemMenu) | Mask(ActivationContext::inventory) |
                Mask(ActivationContext::favorites) | Mask(ActivationContext::map) |
                Mask(ActivationContext::stats) | Mask(ActivationContext::cursor) |
                Mask(ActivationContext::book) | Mask(ActivationContext::journal) |
                Mask(ActivationContext::lockpicking) | Mask(ActivationContext::marketplace);
            constexpr std::uint32_t nonLocalContexts =
                Mask(ActivationContext::gameplay) | Mask(ActivationContext::global) |
                Mask(ActivationContext::externalOverlay) | Mask(ActivationContext::sexLabScene);
            const auto text = Lower(action);
            const auto identity = Lower(std::string(owner) + " " + std::string(action));
            const bool menuScoped = (contextMask & menuContexts) != 0 &&
                (contextMask & nonLocalContexts) == 0;
            const bool customUiIdentity = Has(identity, "menu") || Has(identity, "mcm") ||
                Has(identity, " ui") || Has(identity, "ui_") || Has(identity, "inventory") ||
                Has(identity, "loot") || Has(identity, "explorer") || Has(identity, "wheel") ||
                Has(identity, "widget") || Has(identity, "racemenu") || Has(identity, "skyui") ||
                Has(identity, "photo mode") || Has(identity, "photomode") || Has(identity, "modex");
            if (!menuScoped && !customUiIdentity) return false;
            static constexpr std::array<std::string_view, 8> entryActions{
                "open", "close", "toggle", "show", "hide", "launch", "display", "activate menu"
            };
            const bool entryAction = std::ranges::any_of(entryActions,
                [&](const auto word) { return Has(text, word); }) && !Has(text, "toggle menus");
            if (entryAction) return false;
            // A structured setting owned by a known custom UI and not opening
            // that UI is an internal navigation/action key. It is not an
            // independently active gameplay shortcut.
            if (customUiIdentity && detector == "StructuredConfigScanner") return true;

            static constexpr std::array<std::string_view, 36> localActions{
                "next tab", "previous tab", "prev tab", "tab left", "tab right",
                "next page", "previous page", "prev page", "page left", "page right",
                "scroll up", "scroll down", "scroll list", "next item", "previous item",
                "prev item", "next entry", "previous entry", "accept selection", "cancel selection",
                "menu up", "menu down", "menu left", "menu right", "sort order", "toggle focus",
                "group add", "group remove", "name search", "quantity change", "clipboard",
                "reorder", "rename", "rotate left", "rotate right", "menu navigation"
            };
            return std::ranges::any_of(localActions, [&](const auto phrase) { return Has(text, phrase); }) ||
                Has(text, "ui only") || text == "take" || text.ends_with(" take") ||
                text == "take all" || text.ends_with(" take all");
        } catch (...) {
            return false;
        }
    }
}
