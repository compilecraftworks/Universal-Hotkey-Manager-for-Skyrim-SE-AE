#include "UHI/scanners/PexBytecodeScanner.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace
{
    void U8(std::vector<char>& data, const std::uint8_t value) { data.push_back(static_cast<char>(value)); }
    void U16(std::vector<char>& data, const std::uint16_t value)
    {
        U8(data, static_cast<std::uint8_t>(value >> 8U));
        U8(data, static_cast<std::uint8_t>(value));
    }
    void U32(std::vector<char>& data, const std::uint32_t value)
    {
        U16(data, static_cast<std::uint16_t>(value >> 16U));
        U16(data, static_cast<std::uint16_t>(value));
    }
    void U64(std::vector<char>& data, const std::uint64_t value)
    {
        U32(data, static_cast<std::uint32_t>(value >> 32U));
        U32(data, static_cast<std::uint32_t>(value));
    }
    void String(std::vector<char>& data, const std::string_view value)
    {
        U16(data, static_cast<std::uint16_t>(value.size()));
        data.insert(data.end(), value.begin(), value.end());
    }
    void Identifier(std::vector<char>& data, const std::uint16_t index) { U8(data, 1); U16(data, index); }
    void StringValue(std::vector<char>& data, const std::uint16_t index) { U8(data, 2); U16(data, index); }
    void Integer(std::vector<char>& data, const std::int32_t value) { U8(data, 3); U32(data, value); }
    void None(std::vector<char>& data) { U8(data, 0); }

    std::vector<char> MakePex()
    {
        enum StringIndex : std::uint16_t {
            empty, object, parent, onConfigInit, noneType, registerForKey, self, noneVar, intType,
            addKeyMapOption, hotkeyLabel, keyVariable, hotkeyProperty, temporary
        };
        constexpr std::array strings{
            "", "TestMcmScript", "Quest", "OnConfigInit", "None", "RegisterForKey", "self",
            "::nonevar", "Int", "AddKeyMapOption", "$UHI_TEST_HOTKEY", "::Hotkey_var", "Hotkey", "::temp0"
        };
        std::vector<char> data;
        for (const auto byte : std::array<std::uint8_t, 4>{ 0xFA, 0x57, 0xC0, 0xDE }) U8(data, byte);
        U8(data, 3); U8(data, 9); U16(data, 1); U64(data, 0);
        String(data, "TestMcmScript.psc"); String(data, "tester"); String(data, "computer");
        U16(data, static_cast<std::uint16_t>(strings.size()));
        for (const auto value : strings) String(data, value);
        U8(data, 0); // no debug package
        U16(data, 0); // user flags
        U16(data, 1); // objects
        U16(data, object); U32(data, 0); U16(data, parent); U16(data, empty); U32(data, 0); U16(data, empty);

        U16(data, 1); // variables
        U16(data, keyVariable); U16(data, intType); U32(data, 0); Integer(data, 68);
        U16(data, 1); // properties
        U16(data, hotkeyProperty); U16(data, intType); U16(data, empty); U32(data, 0); U8(data, 4);
        U16(data, keyVariable);
        U16(data, 1); // states
        U16(data, empty); U16(data, 1); // state and functions
        U16(data, onConfigInit);
        U16(data, noneType); U16(data, empty); U32(data, 0); U8(data, 0); // function header
        U16(data, 0); U16(data, 1); U16(data, temporary); U16(data, intType); // params, locals
        U16(data, 4); // instructions
        U8(data, 28); Identifier(data, hotkeyProperty); Identifier(data, self); Identifier(data, temporary);
        U8(data, 23); Identifier(data, registerForKey); Identifier(data, self); Identifier(data, noneVar);
        Integer(data, 1); Identifier(data, temporary);
        U8(data, 23); Identifier(data, addKeyMapOption); Identifier(data, self); Identifier(data, noneVar);
        Integer(data, 2); StringValue(data, hotkeyLabel); Identifier(data, temporary);
        U8(data, 26); None(data);
        return data;
    }
}

int main(const int argc, char** argv)
{
    if (argc > 1) {
        for (int argument = 1; argument < argc; ++argument) {
            const std::filesystem::path path(argv[argument]);
            std::ifstream stream(path, std::ios::binary);
            const std::string bytes((std::istreambuf_iterator<char>(stream)), {});
            const auto scanned = UHI::Scanners::PexBytecodeScanner{}.ScanContent(path, bytes);
            std::cout << path.string() << "\t" << scanned.size() << "\n";
            for (const auto& record : scanned) {
                std::cout << record.owner << '\t' << record.action << '\t' << record.settingName << '\t'
                          << record.settingSection << '\t' << record.binding << '\t' << record.rawBinding << "\n";
            }
        }
        return 0;
    }
    const auto bytes = MakePex();
    const auto records = UHI::Scanners::PexBytecodeScanner{}.ScanContent(
        std::filesystem::path("TestMcmScript.pex"), std::string_view(bytes.data(), bytes.size()));
    const auto mcm = std::ranges::find_if(records, [](const auto& record) {
        return record.action == "$UHI_TEST_HOTKEY" && record.binding == "F10" &&
            record.detector == "PexBytecodeScanner" && record.settingName == "Hotkey";
    });
    auto corrupt = bytes;
    corrupt.resize(corrupt.size() - 3);
    const auto rejected = UHI::Scanners::PexBytecodeScanner{}.ScanContent(
        std::filesystem::path("Corrupt.pex"), std::string_view(corrupt.data(), corrupt.size()));
    // RegisterForKey and AddKeyMapOption refer to the same MCM setting in this
    // fixture. The implementation call must be consolidated into the labelled
    // entry instead of creating a false conflict with itself.
    if (records.size() != 1 || mcm == records.end() || !rejected.empty()) {
        std::cerr << "PEX bytecode scanner test failed\n";
        return 1;
    }
    std::cout << "PEX bytecode scanner test passed\n";
    return 0;
}
