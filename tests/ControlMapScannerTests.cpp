#include "UHI/scanners/ControlMapScanner.h"
#include "UHI/InputCodeFormatter.h"

#include <filesystem>
#include <fstream>
#include <iostream>

int main()
{
    const auto path = std::filesystem::temp_directory_path() / "uhi_controlmap_test.txt";
    {
        std::ofstream output(path);
        output << "// format documentation, not a binding\n";
        output << "Forward\t\t\t\t0x11\t\t\t\t0x0\t0x1000\t\t\t0\t0\t0\n";
        output << "Open menu\t0x44,0x57\t0xff\t0x0001\t1\t1\t1\n";
        output << "Previous focus\t\t0x2a+0x0f\t0xff\t0x2000+0x0020\t\t0\t0\t0\n";
    }
    const auto records = UHI::Scanners::ControlMapScanner{}.Scan(path);
    const auto memoryRecords = UHI::Scanners::ControlMapScanner{}.ScanContent(path,
        "Jump 0x39 0xff 0x1000 0 0 0\n");
    const auto linkedEventRecords = UHI::Scanners::ControlMapScanner{}.ScanContent(path,
        "Cancel\t\t!0,Favorites,!0,Tween Menu,!0,Pause\t\t!0,Favorites,!0,Tween Menu,!0,Pause"
        "\t\t0x2000\t\t\t0\t0\t0\t0x8\n");
    const auto contextRecords = UHI::Scanners::ControlMapScanner{}.ScanContent(path,
        "// Main Gameplay\nUse 0x12 0xff 0xff 0 0 0\n\n// Menu Mode\nAccept 0x1c 0xff 0xff 0 0 0\n"
        "\n// Inventory\nChargeItem 0x14 0xff 0xff 0 0 0\n\n// Map Menu\nMapLookMode 0x32 0xff 0xff 0 0 0\n"
        "\n// Console\nConsoleKey 0x29 0xff 0xff 0 0 0\n"
        "\n// Debug Text\nDebugTextKey 0x3b 0xff 0xff 0 0 0\n"
        "\n// Debug Overlay\nDebugOverlayKey 0x3c 0xff 0xff 0 0 0\n"
        "\n// Debug Map Menu\nDebugMapKey 0x3d 0xff 0xff 0 0 0\n"
        "\n// Creations Menu\nCreationsKey 0x3e 0xff 0xff 0 0 0\n");
    const auto known = UHI::Scanners::ControlMapScanner{}.KnownGameDefaults(path.parent_path());
    std::filesystem::remove(path);
    if (records.size() != 8 || records[0].binding != "W" || records[1].binding != "LMB" ||
        records[2].binding != "A" || records[3].binding != "F10" || records[4].binding != "F11" ||
        records[5].binding != "DUp" || records[6].binding != "LShift+Tab" || records[7].binding != "B+Back" ||
        records[3].rawBinding != "0x44,0x57" || records[1].codeSystem != "controlmap mouse button ID" ||
        !records[3].editable || memoryRecords.size() != 2 || memoryRecords[0].binding != "Space" ||
        linkedEventRecords.size() != 1 || linkedEventRecords[0].action != "Cancel" ||
        linkedEventRecords[0].device != "gamepad" || linkedEventRecords[0].binding.find('!') != std::string::npos ||
        memoryRecords[0].contextMask != static_cast<std::uint32_t>(UHI::ActivationContext::gameplay) ||
        contextRecords.size() != 4 ||
        contextRecords[1].contextMask != static_cast<std::uint32_t>(UHI::ActivationContext::menuMode) ||
        contextRecords[2].contextMask != static_cast<std::uint32_t>(UHI::ActivationContext::inventory) ||
        contextRecords[3].contextMask != static_cast<std::uint32_t>(UHI::ActivationContext::map) ||
        memoryRecords[1].binding != "A" || known.size() < 50 || known[0].action != "Forward" ||
        known[0].binding != "W" || known[0].evidenceLine != 15 || known[0].editable ||
        known[0].evidencePath.string().find(".bsa") != std::string::npos ||
        UHI::FormatControlMapGamepadCode(0x000B) != "Left Stick" ||
        UHI::FormatControlMapGamepadCode(0x000C) != "Right Stick" ||
        UHI::FormatControlMapGamepadCode(0x0040) != "Left Stick Click" ||
        UHI::FormatControlMapGamepadCode(0x0080) != "Right Stick Click" ||
        UHI::FormatSkseGamepadCode(272) != "Left Stick Click" ||
        UHI::FormatSkseGamepadCode(273) != "Right Stick Click") {
        std::cerr << "Control map scanner test failed\n";
        return 1;
    }
    return 0;
}
