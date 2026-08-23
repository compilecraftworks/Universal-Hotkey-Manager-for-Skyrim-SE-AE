#include "UHI/HotkeyViewModel.h"

#include <iostream>

int main()
{
    UHI::Registry registry;
    registry.Add({ .owner = "A", .action = "One", .binding = "Page Up", .device = "keyboard",
        .contextMask = static_cast<std::uint32_t>(UHI::ActivationContext::gameplay),
        .contextConfidence = UHI::ContextConfidence::confirmed });
    registry.Add({ .owner = "B", .action = "Two", .binding = "Page Up", .device = "keyboard",
        .contextMask = static_cast<std::uint32_t>(UHI::ActivationContext::gameplay),
        .contextConfidence = UHI::ContextConfidence::confirmed });
    registry.Add({ .owner = "C", .action = "Three", .binding = "LMB", .device = "mouse" });
    registry.Add({ .owner = "ReShade", .action = "Toggle effects", .binding = "F6", .device = "keyboard" });
    registry.Add({ .owner = "Pad Mod", .action = "Confirm", .binding = "A", .device = "gamepad" });
    registry.Add({ .owner = "Keyboard Mod", .action = "Action", .binding = "A", .device = "keyboard" });
    registry.Add({ .owner = "Candidate", .action = "Unresolved", .binding = "Numeric 68 (encoding unknown)",
        .device = "unknown", .conflictEligible = false });
    registry.Add({ .owner = "Candidate", .action = "Input marker only", .binding = "Input API usage",
        .device = "keyboard", .conflictEligible = false });
    registry.Add({ .owner = "Custom UI", .action = "Next tab", .binding = "F8", .device = "keyboard",
        .uiLocalOnly = true });
    registry.Add({ .owner = "Static PEX", .action = "Constant", .binding = "F12", .device = "keyboard",
        .detector = "PexBytecodeScanner", .runtimeActive = false });

    const auto groups = UHI::BuildHotkeyView(registry);
    if (groups.size() != 3 || groups[0].device != "keyboard" || groups[0].entries.size() != 4 ||
        groups[0].entries[0].displayBinding != "PgUp" || !groups[0].entries[0].conflict ||
        !groups[0].entries[1].conflict || groups[1].entries[0].conflict ||
        groups[0].entries[2].category != UHI::HotkeyCategory::external) {
        std::cerr << "Hotkey view model test failed\n";
        return 1;
    }
    if (groups[0].entries[3].conflict || groups[2].entries[0].conflict) {
        std::cerr << "Cross-device conflict filtering failed\n";
        return 1;
    }
    const auto external = UHI::FilterHotkeyView(groups, UHI::HotkeyCategory::external);
    if (external.size() != 1 || external[0].entries.size() != 1 ||
        external[0].entries[0].displayBinding != "F6") {
        std::cerr << "Hotkey view filter test failed\n";
        return 1;
    }
    return 0;
}
