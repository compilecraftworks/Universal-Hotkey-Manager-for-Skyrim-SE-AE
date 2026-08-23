#include "UHI/writers/ConfigFileWriter.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>

int main()
{
    const auto root = std::filesystem::temp_directory_path() / "uhi_config_writer_test";
    std::filesystem::create_directories(root);
    const auto ini = root / "Settings.ini";
    const auto json = root / "Settings.json";
    const auto toml = root / "Settings.toml";
    const auto yaml = root / "Settings.yaml";
    const auto cfg = root / "Settings.cfg";
    const auto properties = root / "Settings.properties";
    { std::ofstream out(ini); out << "[Input]\nToggleKey = 87 ; keep comment\n"; }
    { std::ofstream out(json); out << "{\"ToggleKey\":87,\"Chord\":[17,87]}\n"; }
    { std::ofstream out(toml); out << "[input]\nhotkey = \"F7\" # keep comment\n"; }
    { std::ofstream out(yaml); out << "input:\n  hotkey: F7 # keep comment\n"; }
    { std::ofstream out(cfg); out << "Hotkey=DIK_F7\n"; }
    { std::ofstream out(properties); out << "input.hotkey = 65\n"; }
    UHI::Writers::ConfigFileWriter writer;
    const bool iniOk = writer.SetBinding(ini, 2, "ToggleKey", "87", "88");
    const bool jsonOk = writer.SetBinding(json, 1, "ToggleKey", "87", "89");
    const bool staleRejected = !writer.SetBinding(json, 1, "ToggleKey", "87", "90");
    const bool arrayOk = writer.SetBinding(json, 1, "Chord", "[17,87]", "[16,89]");
    const bool tomlOk = writer.SetBinding(toml, 2, "hotkey", "F7", "F8");
    const bool yamlOk = writer.SetBinding(yaml, 2, "hotkey", "F7", "F8");
    const bool cfgOk = writer.SetBinding(cfg, 1, "Hotkey", "DIK_F7", "DIK_F8");
    const bool propertiesOk = writer.SetBinding(properties, 1, "input.hotkey", "65", "66");
    std::ifstream iniInput(ini), jsonInput(json), tomlInput(toml), yamlInput(yaml), cfgInput(cfg),
        propertiesInput(properties);
    const std::string iniText((std::istreambuf_iterator<char>(iniInput)), {});
    const std::string jsonText((std::istreambuf_iterator<char>(jsonInput)), {});
    const std::string tomlText((std::istreambuf_iterator<char>(tomlInput)), {});
    const std::string yamlText((std::istreambuf_iterator<char>(yamlInput)), {});
    const std::string cfgText((std::istreambuf_iterator<char>(cfgInput)), {});
    const std::string propertiesText((std::istreambuf_iterator<char>(propertiesInput)), {});
    iniInput.close();
    jsonInput.close();
    tomlInput.close();
    yamlInput.close();
    cfgInput.close();
    propertiesInput.close();
    const bool backups = std::filesystem::exists(ini.string() + ".uhi.bak") &&
        std::filesystem::exists(json.string() + ".uhi.bak") &&
        std::filesystem::exists(toml.string() + ".uhi.bak") &&
        std::filesystem::exists(yaml.string() + ".uhi.bak") &&
        std::filesystem::exists(cfg.string() + ".uhi.bak") &&
        std::filesystem::exists(properties.string() + ".uhi.bak");
    std::filesystem::remove_all(root);
    if (!iniOk || !jsonOk || !staleRejected || !arrayOk || !tomlOk || !yamlOk || !cfgOk ||
        !propertiesOk || !backups ||
        iniText.find("ToggleKey = 88 ; keep comment") == std::string::npos ||
        jsonText != "{\"ToggleKey\":89,\"Chord\":[16,89]}\n" ||
        tomlText.find("hotkey = \"F8\" # keep comment") == std::string::npos ||
        yamlText.find("hotkey: F8 # keep comment") == std::string::npos ||
        cfgText != "Hotkey=DIK_F8\n" || propertiesText != "input.hotkey = 66\n") {
        std::cerr << "Config file writer test failed\n";
        return 1;
    }
    return 0;
}
