#include "UHI/HotkeyViewRenderer.h"

#include <iostream>
#include <string>

int main()
{
    UHI::Registry registry;
    registry.Add({ .owner = "ReShade", .action = "Toggle", .binding = "Page Up", .device = "keyboard" });

    UHI::HotkeyViewRenderer renderer;
    renderer.SetState({ .open = true, .category = UHI::HotkeyCategory::all });
    std::string lastText;
    std::string lastEntry;
    renderer.Render(registry,
        [&](const std::string_view text) { if (lastText.empty()) lastText = std::string(text); },
        [&](const UHI::HotkeyViewEntry& entry) { lastEntry = entry.displayBinding; });

    if (lastText != "All" || lastEntry != "PgUp") {
        std::cerr << "Hotkey view renderer test failed\n";
        return 1;
    }
    return 0;
}
