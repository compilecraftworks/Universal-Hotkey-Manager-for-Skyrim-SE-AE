#include "UHI/scanners/EnbScanner.h"

#include <filesystem>
#include <fstream>
#include <iostream>

int main()
{
    const auto path = std::filesystem::temp_directory_path() / "uhi_enblocal.ini";
    {
        std::ofstream output(path);
        output << "[INPUT]\nKeyCombination=16\nKeyUseEffect=123\nKeyEditor=13\nKeyShowFPS=106\nKeyScreenshot=44\n";
    }
    const auto records = UHI::Scanners::EnbScanner{}.Scan(path);
    std::filesystem::remove(path);
    if (records.size() != 4 || records[0].binding != "Shift+F12" || records[1].binding != "Shift+Ent" ||
        records[2].binding != "Num*" || records[3].binding != "PrtSc") {
        std::cerr << "ENB scanner test failed\n";
        return 1;
    }
    return 0;
}
