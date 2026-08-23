#pragma once

#include <filesystem>
#include <cstdint>
#include <string>
#include <vector>

namespace UHI
{
    enum class Confidence
    {
        confirmed,
        inferred,
        candidate
    };

    enum class ScanStage
    {
        configuration,
        scripts,
        nativePlugins,
        runtime
    };

    // Engine-aligned activation contexts. A physical key is only a confirmed
    // conflict when two different actions can receive it in an overlapping
    // context. Zero deliberately means "not proven", not "global".
    enum class ActivationContext : std::uint32_t
    {
        unknown = 0,
        gameplay = 1U << 0,
        menuMode = 1U << 1,
        console = 1U << 2,
        itemMenu = 1U << 3,
        inventory = 1U << 4,
        favorites = 1U << 5,
        map = 1U << 6,
        stats = 1U << 7,
        cursor = 1U << 8,
        book = 1U << 9,
        journal = 1U << 10,
        lockpicking = 1U << 11,
        externalOverlay = 1U << 12,
        debugText = 1U << 13,
        debugOverlay = 1U << 14,
        tfcMode = 1U << 15,
        mapDebug = 1U << 16,
        marketplace = 1U << 17,
        favor = 1U << 18,
        sexLabScene = 1U << 19,
        global = 1U << 31
    };

    enum class ContextConfidence : std::uint8_t
    {
        unknown,
        inferred,
        confirmed
    };

    struct HotkeyRecord
    {
        std::string owner;
        std::string action;
        std::string binding;
        std::string rawBinding;
        // Structured settings retain their original key and section so a
        // writer can update the exact value without guessing from the
        // human-readable action label.
        std::string settingName;
        std::string settingSection;
        std::string codeSystem{ "unknown" };
        std::string device{ "keyboard" };
        std::string detector;
        // Optional post-scan category override derived from reliable external
        // evidence such as an ESP/ESM/ESL master dependency.
        std::string categoryHint;
        Confidence confidence{ Confidence::candidate };
        std::filesystem::path evidencePath;
        std::size_t evidenceLine{};
        ScanStage stage{ ScanStage::configuration };
        bool editable{ false };
        // True only when the scanner has evidence that this is the effective
        // runtime binding, rather than a preset/default/source-code candidate.
        // Synthetic records default to active; static scanners explicitly
        // downgrade their findings until runtime corroboration exists.
        bool runtimeActive{ true };
        // A scanner can find evidence that a mod consumes input without knowing
        // the actual physical key. Keep that evidence visible, but never turn it
        // into a misleading key-conflict warning.
        bool conflictEligible{ true };
        // A key proven to operate only inside a particular mod's already-open
        // custom UI is retained in raw reports, but omitted from the normal
        // shortcut inventory and physical conflict analysis.
        bool uiLocalOnly{ false };
        std::uint32_t contextMask{};
        ContextConfidence contextConfidence{ ContextConfidence::unknown };
    };

    std::string NormalizeBinding(std::string value);

    // Returns the compact label used by the device view. The original binding is
    // retained in HotkeyRecord::binding for tooltips and reports.
    std::string CompactBindingLabel(const std::string& binding);

    // Returns a short semantic label for the physical-device map while the
    // complete action description remains available in details and tooltips.
    std::string CompactActionLabel(const std::string& action);
    std::string ActivationContextLabel(std::uint32_t mask);

    // Turns internal archive, plugin, script and configuration identifiers
    // into the recognizable mod name used throughout the UI.
    std::string CanonicalModDisplayName(std::string value);

    // Removes storage/type/localization syntax from labels before they are
    // shown to users while preserving the original settingName/rawBinding.
    void NormalizeRecordDisplayNames(HotkeyRecord& record) noexcept;
}
