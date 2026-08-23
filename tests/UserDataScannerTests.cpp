#include "UHI/scanners/UserDataScanner.h"

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string_view>
#include <vector>

namespace
{
    void U32(std::vector<char>& data, const std::uint32_t value)
    {
        for (std::uint32_t shift = 0; shift < 32; shift += 8) data.push_back(static_cast<char>(value >> shift));
    }
    void U64(std::vector<char>& data, const std::uint64_t value)
    {
        U32(data, static_cast<std::uint32_t>(value));
        U32(data, static_cast<std::uint32_t>(value >> 32U));
    }
    std::uint32_t Tag(const std::string_view value)
    {
        return (static_cast<std::uint32_t>(static_cast<unsigned char>(value[0])) << 24U) |
            (static_cast<std::uint32_t>(static_cast<unsigned char>(value[1])) << 16U) |
            (static_cast<std::uint32_t>(static_cast<unsigned char>(value[2])) << 8U) |
            static_cast<unsigned char>(value[3]);
    }
    void Chunk(std::vector<char>& plugin, const std::string_view type, const std::vector<char>& payload = {})
    {
        U32(plugin, Tag(type)); U32(plugin, 1); U32(plugin, static_cast<std::uint32_t>(payload.size()));
        plugin.insert(plugin.end(), payload.begin(), payload.end());
    }
    void MakeCoSave(const std::filesystem::path& path)
    {
        std::vector<char> registrations;
        U32(registrations, 276); U32(registrations, 2);
        U64(registrations, 0x12345678ULL); U64(registrations, 0x9ABCDEF0ULL);
        std::vector<char> plugin;
        Chunk(plugin, "KEYR"); Chunk(plugin, "REGS", registrations); Chunk(plugin, "REGE");
        std::vector<char> file{ 'S', 'K', 'S', 'E' };
        U32(file, 1); U32(file, 0x02020600); U32(file, 0x01061170); U32(file, 1);
        U32(file, 0); U32(file, 3); U32(file, static_cast<std::uint32_t>(plugin.size()));
        file.insert(file.end(), plugin.begin(), plugin.end());
        std::ofstream output(path, std::ios::binary | std::ios::trunc);
        output.write(file.data(), static_cast<std::streamsize>(file.size()));
    }
}

int main()
{
    const auto root = std::filesystem::temp_directory_path() / "uhi_user_data_test";
    std::error_code error;
    std::filesystem::remove_all(root, error);
    const auto documents = root / "Documents";
    const auto gameBase = documents / "My Games" / "Skyrim Special Edition";
    std::filesystem::create_directories(gameBase / "JCUser");
    std::filesystem::create_directories(gameBase / "Saves");
    {
        std::ofstream json(gameBase / "JCUser" / "hotkeys.txt");
        json << R"({"MenuHotkey":259})";
    }
    MakeCoSave(gameBase / "Saves" / "Current.skse");

    const auto records = UHI::Scanners::UserDataScanner{}.Scan(root / "Game", "Current.ess", {}, documents);
    const auto persistent = std::ranges::find_if(records, [](const auto& record) {
        return record.binding == "M4" && record.device == "mouse" &&
            record.detector == "PersistentSettingsScanner" && record.stage == UHI::ScanStage::runtime;
    });
    const auto registeredCount = std::ranges::count_if(records, [](const auto& record) {
        return record.binding == "A" && record.device == "gamepad" &&
            record.detector == "SkseCoSaveKeyRegistry" && record.conflictEligible;
    });
    const auto cancelled = UHI::Scanners::UserDataScanner{}.Scan(
        root / "Game", "Current.ess", [] { return true; }, documents);
    std::filesystem::remove_all(root, error);
    if (persistent == records.end() || registeredCount != 2 || !cancelled.empty()) {
        std::cerr << "User data scanner test failed\n";
        return 1;
    }
    std::cout << "User data scanner test passed\n";
    return 0;
}
