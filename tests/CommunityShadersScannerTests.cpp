#include "UHI/scanners/CommunityShadersScanner.h"

#include <filesystem>
#include <fstream>
#include <iostream>

int main()
{
    const auto path = std::filesystem::temp_directory_path() / "uhi_cs_settings.json";
    {
        std::ofstream output(path);
        output << R"({"Menu":{"ToggleKey":35,"OverlayToggleKey":121,"CSEditorToggleKey":[16,35],"EffectToggleKey":262145}})";
    }
    const auto records = UHI::Scanners::CommunityShadersScanner{}.Scan(path);
    std::filesystem::remove(path);
    if (records.size() != 4 || records[0].binding != "End" || records[1].binding != "F10" ||
        records[2].binding != "Shift+End" || records[3].binding != "LMB" ||
        records[3].device != "mouse" || records[0].codeSystem.find("Windows VK") == std::string::npos) {
        std::cerr << "Community Shaders scanner test failed\n";
        return 1;
    }
    return 0;
}
