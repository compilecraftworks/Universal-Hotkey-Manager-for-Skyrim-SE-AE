#include "UHI/scanners/DllScanner.h"
#include "UHI/scanners/PexScanner.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>

int main()
{
    const auto root = std::filesystem::temp_directory_path() / "uhi_static_test";
    std::filesystem::create_directories(root / "Scripts");
    std::filesystem::create_directories(root / "Plugins");
    {
        std::ofstream pex(root / "Scripts" / "Example.pex", std::ios::binary);
        pex << "\x01\x02RegisterForKey(68)\x00";
        std::ofstream psc(root / "Scripts" / "SourceScript.psc");
        psc << "Event OnInit()\n  RegisterForKey(69)\nEndEvent\n";
        std::ofstream dll(root / "Plugins" / "Example.dll", std::ios::binary);
        const char dllBytes[] = "MZ\0GetAsyncKeyState\0ScanCode=68\0";
        dll.write(dllBytes, sizeof(dllBytes) - 1);
        std::ofstream vkDll(root / "Plugins" / "Win32Example.dll", std::ios::binary);
        const char vkBytes[] = "MZ\0GetAsyncKeyState\0KeyCode=35\0";
        vkDll.write(vkBytes, sizeof(vkBytes) - 1);
        std::ofstream commonLibDll(root / "Plugins" / "CommonLibExample.dll", std::ios::binary);
        const char commonLibBytes[] = "MZ\0Hotkey=35\0";
        commonLibDll.write(commonLibBytes, sizeof(commonLibBytes) - 1);
        std::ofstream typeModeDll(root / "Plugins" / "zzzz_TypeMode.dll", std::ios::binary);
        const char typeModeBytes[] =
            "MZ\0Data/SKSE/Plugins/TypeMode_Settings.json\0TypeMode_k\0Typing mode Key\0ButtonEvent\0";
        typeModeDll.write(typeModeBytes, sizeof(typeModeBytes) - 1);
    }

    const auto pexRecords = UHI::Scanners::PexScanner{}.Scan(root);
    const auto dllRecords = UHI::Scanners::DllScanner{}.Scan(root);
    std::filesystem::remove_all(root);
    const auto pexKey = std::find_if(pexRecords.begin(), pexRecords.end(), [](const auto& record) {
        return record.action == "Example" && record.binding == "F10" && record.conflictEligible;
    });
    const auto pscKey = std::find_if(pexRecords.begin(), pexRecords.end(), [](const auto& record) {
        return record.owner == "SourceScript" && record.action == "SourceScript" && record.binding == "NumLk" &&
            !record.conflictEligible;
    });
    const auto dllKey = std::find_if(dllRecords.begin(), dllRecords.end(), [](const auto& record) {
        return record.action == "Example" && record.binding == "F10" && record.conflictEligible &&
            record.codeSystem == "DirectInput scan code";
    });
    const auto vkFallback = std::find_if(dllRecords.begin(), dllRecords.end(), [](const auto& record) {
        return record.owner == "Win32Example" && record.binding == "End" && record.conflictEligible &&
            record.codeSystem.find("Win32-key-API fallback") != std::string::npos;
    });
    const auto skseFallback = std::find_if(dllRecords.begin(), dllRecords.end(), [](const auto& record) {
        return record.owner == "CommonLibExample" && record.binding == "H" && record.conflictEligible &&
            record.codeSystem.find("SKSE/CommonLib fallback") != std::string::npos;
    });
    const auto typeModeDefault = std::find_if(dllRecords.begin(), dllRecords.end(), [](const auto& record) {
        return record.owner == "Typing Mode" && record.action == "Typing Mode" &&
            record.settingName == "TypeMode_k" && record.binding == "F4" &&
            record.detector == "GeneratedSettingsDefaultScanner" && record.runtimeActive;
    });
    if (pexRecords.empty() || dllRecords.empty() || pexKey == pexRecords.end() || pscKey == pexRecords.end() || dllKey == dllRecords.end() ||
        vkFallback == dllRecords.end() || skseFallback == dllRecords.end() ||
        typeModeDefault == dllRecords.end() ||
        pexRecords.front().stage != UHI::ScanStage::scripts ||
        dllRecords.front().stage != UHI::ScanStage::nativePlugins) {
        std::cerr << "Static scanner test failed\n";
        return 1;
    }
    std::cout << "Static scanner test passed\n";
    return 0;
}
