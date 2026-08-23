#include "UHI/writers/ControlMapWriter.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

int main()
{
    const auto path = std::filesystem::temp_directory_path() / "uhi_controlmap_writer_test.txt";
    {
        std::ofstream output(path);
        output << "Forward\t\t\t\t17\t\t\t-1\t-1\t\t0\t0\t0\n";
        output << "Activate\t\t18\t\t2\t7\t\t0\t0\t0\n";
    }
    const UHI::Writers::ControlMapWriter writer;
    const auto keyboard = writer.SetBinding(path, 1, "keyboard", "68");
    const auto mouse = writer.SetBinding(path, 2, "mouse", "4");
    const auto gamepad = writer.SetBinding(path, 2, "gamepad", "9");
    const auto rejected = writer.SetBinding(path, 2, "mouse", "20");
    std::ifstream input(path);
    std::string result((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
    const auto backup = std::filesystem::path(path.string() + ".uhi.bak");
    std::ifstream backupInput(backup);
    std::string backupText((std::istreambuf_iterator<char>(backupInput)), std::istreambuf_iterator<char>());
    input.close();
    backupInput.close();
    std::filesystem::remove(path);
    std::filesystem::remove(backup);
    if (!keyboard || !mouse || !gamepad || rejected ||
        result != "Forward\t\t\t\t0x44\t\t\t-1\t-1\t\t0\t0\t0\nActivate\t\t18\t\t0x4\t0x0009\t\t0\t0\t0\n" ||
        backupText != "Forward\t\t\t\t17\t\t\t-1\t-1\t\t0\t0\t0\nActivate\t\t18\t\t2\t7\t\t0\t0\t0\n") {
        std::cerr << "Control map writer test failed: keyboard=" << keyboard << " mouse=" << mouse
                  << " gamepad=" << gamepad << " rejected=" << rejected << "\nresult=[" << result
                  << "]\nbackup=[" << backupText << "]\n";
        return 1;
    }
    std::cout << "Control map writer test passed\n";
    return 0;
}
