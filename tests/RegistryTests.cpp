#include "UHI/Registry.h"

#include <iostream>

namespace
{
    UHI::HotkeyRecord MakeRecord(std::string owner, std::string binding, std::size_t line)
    {
        return {
            .owner = std::move(owner),
            .action = "Action",
            .binding = std::move(binding),
            .detector = "RegistryTest",
            .evidencePath = "test.ini",
            .evidenceLine = line,
            .contextMask = static_cast<std::uint32_t>(UHI::ActivationContext::gameplay),
            .contextConfidence = UHI::ContextConfidence::confirmed
        };
    }
}

int main()
{
    UHI::Registry corroborated;
    corroborated.Add(MakeRecord("SameMod", "F7", 1));
    corroborated.Add(MakeRecord("SameMod", "F7", 2));
    if (!corroborated.Conflicts().empty()) {
        std::cerr << "Registry treated corroborating evidence as a conflict\n";
        return 1;
    }
    auto distinctAction = MakeRecord("SameMod", "F7", 3);
    distinctAction.action = "Different action";
    corroborated.Add(std::move(distinctAction));
    if (corroborated.Conflicts().size() != 1) {
        std::cerr << "Registry missed two distinct actions on the same binding\n";
        return 1;
    }

    UHI::Registry stickInputs;
    auto move = MakeRecord("Skyrim", "Left Stick", 10);
    move.device = "gamepad";
    move.action = "Move";
    stickInputs.Add(std::move(move));
    auto sneak = MakeRecord("Skyrim", "Left Stick Click", 11);
    sneak.device = "gamepad";
    sneak.action = "Sneak";
    stickInputs.Add(std::move(sneak));
    if (!stickInputs.Conflicts().empty()) {
        std::cerr << "Registry treated a stick axis and its click button as the same input\n";
        return 1;
    }

    UHI::Registry contexts;
    auto field = MakeRecord("Field Mod", "F8", 20);
    contexts.Add(std::move(field));
    auto inventory = MakeRecord("Inventory Mod", "F8", 21);
    inventory.contextMask = static_cast<std::uint32_t>(UHI::ActivationContext::inventory);
    contexts.Add(std::move(inventory));
    if (!contexts.Conflicts().empty() || contexts.ConditionalConflictCount() != 0) {
        std::cerr << "Mutually exclusive contexts were treated as a conflict\n";
        return 1;
    }
    UHI::Registry controlMapReuse;
    auto accept = MakeRecord("Skyrim default controls", "A", 40);
    accept.action = "Accept";
    accept.detector = "ControlMapScanner";
    accept.contextMask = static_cast<std::uint32_t>(UHI::ActivationContext::menuMode);
    controlMapReuse.Add(std::move(accept));
    auto click = MakeRecord("Skyrim default controls", "A", 41);
    click.action = "Click";
    click.detector = "ControlMapScanner";
    click.contextMask = static_cast<std::uint32_t>(UHI::ActivationContext::inventory);
    controlMapReuse.Add(std::move(click));
    if (!controlMapReuse.Conflicts().empty() || controlMapReuse.ConditionalConflictCount() != 0) {
        std::cerr << "Intentional controlmap context reuse was treated as a conflict\n";
        return 1;
    }
    auto menuCommon = MakeRecord("Menu Mod", "F8", 22);
    menuCommon.contextMask = static_cast<std::uint32_t>(UHI::ActivationContext::menuMode);
    contexts.Add(std::move(menuCommon));
    if (contexts.Conflicts().size() != 1) {
        std::cerr << "Menu common context did not overlap a specialized menu context\n";
        return 1;
    }
    UHI::Registry nestedContexts;
    auto generalGameplay = MakeRecord("Gameplay Input", "F6", 50);
    nestedContexts.Add(std::move(generalGameplay));
    auto sceneOnly = MakeRecord("SexLab Scene Input", "F6", 51);
    sceneOnly.contextMask = static_cast<std::uint32_t>(UHI::ActivationContext::sexLabScene);
    nestedContexts.Add(std::move(sceneOnly));
    if (!nestedContexts.Conflicts().empty() || nestedContexts.ConditionalConflictCount() != 0) {
        std::cerr << "A SexLab scene-only binding conflicted with ordinary gameplay input\n";
        return 1;
    }
    auto secondSceneAction = MakeRecord("SexLab Scene Extension", "F6", 52);
    secondSceneAction.action = "Second scene action";
    secondSceneAction.contextMask = static_cast<std::uint32_t>(UHI::ActivationContext::sexLabScene);
    nestedContexts.Add(std::move(secondSceneAction));
    if (nestedContexts.Conflicts().size() != 1) {
        std::cerr << "Two distinct bindings active in the same SexLab scene were not compared\n";
        return 1;
    }
    UHI::Registry overlayContexts;
    auto overlay = MakeRecord("External Overlay", "F5", 53);
    overlay.contextMask = static_cast<std::uint32_t>(UHI::ActivationContext::externalOverlay);
    overlayContexts.Add(std::move(overlay));
    auto menuInput = MakeRecord("Inventory Input", "F5", 54);
    menuInput.contextMask = static_cast<std::uint32_t>(UHI::ActivationContext::inventory);
    overlayContexts.Add(std::move(menuInput));
    if (overlayContexts.Conflicts().size() != 1) {
        std::cerr << "An external overlay did not intersect the underlying menu context\n";
        return 1;
    }
    UHI::Registry uncertain;
    auto unknownA = MakeRecord("Unknown A", "F9", 23);
    unknownA.contextMask = 0;
    unknownA.contextConfidence = UHI::ContextConfidence::unknown;
    uncertain.Add(std::move(unknownA));
    auto unknownB = MakeRecord("Unknown B", "F9", 24);
    unknownB.contextMask = 0;
    unknownB.contextConfidence = UHI::ContextConfidence::unknown;
    uncertain.Add(std::move(unknownB));
    if (!uncertain.Conflicts().empty() || uncertain.ConditionalConflictCount() != 1) {
        std::cerr << "Unknown contexts were not kept as a conditional overlap\n";
        return 1;
    }

    UHI::Registry activeSettings;
    auto fitting = MakeRecord("SkyrimFittingSystem", "F6", 25);
    fitting.detector = "StructuredConfigScanner";
    fitting.contextMask = 0;
    fitting.contextConfidence = UHI::ContextConfidence::unknown;
    fitting.editable = true;
    activeSettings.Add(std::move(fitting));
    auto vanity = MakeRecord("SkyrimVanitySystem", "F6", 26);
    vanity.detector = "StructuredConfigScanner";
    vanity.contextMask = 0;
    vanity.contextConfidence = UHI::ContextConfidence::unknown;
    vanity.editable = true;
    activeSettings.Add(std::move(vanity));
    if (!activeSettings.Conflicts().empty() || activeSettings.ConditionalConflictCount() != 1) {
        std::cerr << "Unknown active settings were not kept as a conditional overlap\n";
        return 1;
    }

    UHI::Registry mixedConfidence;
    auto provenField = MakeRecord("Confirmed Field", "F7", 27);
    provenField.contextMask = static_cast<std::uint32_t>(UHI::ActivationContext::gameplay);
    provenField.contextConfidence = UHI::ContextConfidence::confirmed;
    mixedConfidence.Add(std::move(provenField));
    auto unresolved = MakeRecord("Unknown Scope", "F7", 28);
    unresolved.contextMask = 0;
    unresolved.contextConfidence = UHI::ContextConfidence::unknown;
    mixedConfidence.Add(std::move(unresolved));
    if (!mixedConfidence.Conflicts().empty() || mixedConfidence.ConditionalConflictCount() != 1) {
        std::cerr << "Confirmed plus unknown context was not kept as a conditional overlap\n";
        return 1;
    }

    UHI::Registry uiLocal;
    auto ordinary = MakeRecord("Gameplay Mod", "F6", 30);
    uiLocal.Add(std::move(ordinary));
    auto localNavigation = MakeRecord("Custom UI", "F6", 31);
    localNavigation.action = "Next tab";
    localNavigation.contextMask = static_cast<std::uint32_t>(UHI::ActivationContext::menuMode);
    localNavigation.uiLocalOnly = true;
    uiLocal.Add(std::move(localNavigation));
    if (!uiLocal.Conflicts().empty() || uiLocal.ConditionalConflictCount() != 0 ||
        uiLocal.Records().size() != 2) {
        std::cerr << "UI-local input was not retained and excluded from conflicts\n";
        return 1;
    }

    UHI::Registry registry(2);
    registry.Add(MakeRecord("A", "F10", 1));
    registry.Add(MakeRecord("A", " f10 ", 1));
    if (registry.Records().size() != 1 || registry.DuplicateCount() != 1) {
        std::cerr << "Registry did not remove a normalized duplicate\n";
        return 1;
    }

    registry.Add(MakeRecord("B", "F10", 2));
    if (registry.Conflicts().size() != 1) {
        std::cerr << "Registry conflict grouping changed after deduplication\n";
        return 1;
    }

    registry.Add(MakeRecord("C", "F11", 3));
    if (!registry.WasTruncated() || registry.DroppedCount() != 1 || registry.Records().size() != 2) {
        std::cerr << "Registry safety limit was not enforced\n";
        return 1;
    }

    UHI::Registry staticCandidates;
    auto staticCandidate = MakeRecord("Static native module", "F4", 60);
    staticCandidate.detector = "StaticCommonLibInputHandler";
    staticCandidate.editable = true;
    staticCandidate.runtimeActive = true;
    staticCandidate.conflictEligible = true;
    staticCandidates.Add(std::move(staticCandidate));
    auto activeSink = MakeRecord("Active native module", "F5", 61);
    activeSink.detector = "ActiveInputSinkAnalyzer";
    activeSink.runtimeActive = true;
    activeSink.conflictEligible = true;
    staticCandidates.Add(std::move(activeSink));
    const auto sanitized = staticCandidates.Records();
    if (sanitized.size() != 2 || sanitized[0].runtimeActive || sanitized[0].editable ||
        sanitized[0].conflictEligible || !sanitized[1].runtimeActive ||
        !sanitized[1].conflictEligible) {
        std::cerr << "Registry did not separate static candidates from active input sinks\n";
        return 1;
    }

    std::cout << "Registry safety tests passed\n";
    return 0;
}
