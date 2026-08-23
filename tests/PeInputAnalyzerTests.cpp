#include "UHI/scanners/PeInputAnalyzer.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string_view>
#include <vector>

namespace
{
    template <class T>
    void Put(std::vector<std::uint8_t>& bytes, const std::size_t offset, const T value)
    {
        std::memcpy(bytes.data() + offset, &value, sizeof(value));
    }

    void PutString(std::vector<std::uint8_t>& bytes, const std::size_t offset, const std::string_view value)
    {
        std::memcpy(bytes.data() + offset, value.data(), value.size());
        bytes[offset + value.size()] = 0;
    }

    void MakePe(const std::filesystem::path& path)
    {
        std::vector<std::uint8_t> bytes(0xC00U);
        Put(bytes, 0, std::uint16_t{ 0x5A4D });
        Put(bytes, 0x3C, std::uint32_t{ 0x80 });
        Put(bytes, 0x80, std::uint32_t{ 0x00004550 });
        Put(bytes, 0x84, std::uint16_t{ 0x8664 });
        Put(bytes, 0x86, std::uint16_t{ 3 });
        Put(bytes, 0x94, std::uint16_t{ 0xF0 });
        constexpr std::size_t optional = 0x98;
        Put(bytes, optional, std::uint16_t{ 0x20B });
        Put(bytes, optional + 24, std::uint64_t{ 0x180000000ULL });
        Put(bytes, optional + 60, std::uint32_t{ 0x400 });
        Put(bytes, optional + 108, std::uint32_t{ 16 });
        Put(bytes, optional + 120, std::uint32_t{ 0x2000 });
        Put(bytes, optional + 124, std::uint32_t{ 40 });
        Put(bytes, optional + 136, std::uint32_t{ 0x3000 });
        Put(bytes, optional + 140, std::uint32_t{ 12 });

        constexpr std::size_t sections = optional + 0xF0;
        PutString(bytes, sections, ".text");
        Put(bytes, sections + 8, std::uint32_t{ 0x200 });
        Put(bytes, sections + 12, std::uint32_t{ 0x1000 });
        Put(bytes, sections + 16, std::uint32_t{ 0x200 });
        Put(bytes, sections + 20, std::uint32_t{ 0x400 });
        Put(bytes, sections + 36, std::uint32_t{ 0x60000020 });
        constexpr std::size_t idata = sections + 40;
        PutString(bytes, idata, ".idata");
        Put(bytes, idata + 8, std::uint32_t{ 0x400 });
        Put(bytes, idata + 12, std::uint32_t{ 0x2000 });
        Put(bytes, idata + 16, std::uint32_t{ 0x400 });
        Put(bytes, idata + 20, std::uint32_t{ 0x600 });
        Put(bytes, idata + 36, std::uint32_t{ 0x40000040 });
        constexpr std::size_t pdata = idata + 40;
        PutString(bytes, pdata, ".pdata");
        Put(bytes, pdata + 8, std::uint32_t{ 0x200 });
        Put(bytes, pdata + 12, std::uint32_t{ 0x3000 });
        Put(bytes, pdata + 16, std::uint32_t{ 0x200 });
        Put(bytes, pdata + 20, std::uint32_t{ 0xA00 });
        Put(bytes, pdata + 36, std::uint32_t{ 0x40000040 });

        // USER32 import descriptor and two 64-bit thunk entries.
        Put(bytes, 0x600, std::uint32_t{ 0x2040 });
        Put(bytes, 0x60C, std::uint32_t{ 0x2080 });
        Put(bytes, 0x610, std::uint32_t{ 0x2060 });
        Put(bytes, 0x640, std::uint64_t{ 0x2090 });
        Put(bytes, 0x648, std::uint64_t{ 0x20B0 });
        Put(bytes, 0x650, std::uint64_t{ 0 });
        PutString(bytes, 0x680, "USER32.dll");
        Put(bytes, 0x690, std::uint16_t{});
        PutString(bytes, 0x692, "GetAsyncKeyState");
        Put(bytes, 0x6B0, std::uint16_t{});
        PutString(bytes, 0x6B2, "RegisterHotKey");

        constexpr std::size_t text = 0x400;
        // mov ecx, VK_F10; call [IAT:GetAsyncKeyState]
        bytes[text] = 0xB9; Put(bytes, text + 1, std::uint32_t{ 0x79 });
        bytes[text + 5] = 0xFF; bytes[text + 6] = 0x15;
        Put(bytes, text + 7, static_cast<std::int32_t>(0x2060 - 0x100B));
        bytes[text + 11] = 0xC3;
        // mov r8d, MOD_CONTROL; mov r9d, VK_F10; call [IAT:RegisterHotKey]
        bytes[text + 0x20] = 0x41; bytes[text + 0x21] = 0xB8;
        Put(bytes, text + 0x22, std::uint32_t{ 0x02 });
        bytes[text + 0x26] = 0x41; bytes[text + 0x27] = 0xB9;
        Put(bytes, text + 0x28, std::uint32_t{ 0x79 });
        bytes[text + 0x2C] = 0xFF; bytes[text + 0x2D] = 0x15;
        Put(bytes, text + 0x2E, static_cast<std::int32_t>(0x2068 - 0x1032));
        bytes[text + 0x32] = 0xC3;

        // CommonLib BSTEventSink<InputEvent*>::ProcessEvent fixture:
        //   event = *events; reference InventoryMenu; require ButtonEvent,
        //   keyboard device and DIK_F7 (0x41).
        constexpr std::size_t processEvent = text + 0x80;
        bytes[processEvent] = 0x48; bytes[processEvent + 1] = 0x8B; bytes[processEvent + 2] = 0x02;
        bytes[processEvent + 3] = 0x48; bytes[processEvent + 4] = 0x8D; bytes[processEvent + 5] = 0x0D;
        Put(bytes, processEvent + 6, static_cast<std::int32_t>(0x2100 - 0x108A));
        bytes[processEvent + 10] = 0x83; bytes[processEvent + 11] = 0x78;
        bytes[processEvent + 12] = 0x0C; bytes[processEvent + 13] = 0x00;
        bytes[processEvent + 14] = 0x75; bytes[processEvent + 15] = 0x0A;
        bytes[processEvent + 16] = 0x83; bytes[processEvent + 17] = 0x78;
        bytes[processEvent + 18] = 0x08; bytes[processEvent + 19] = 0x00;
        bytes[processEvent + 20] = 0x75; bytes[processEvent + 21] = 0x04;
        bytes[processEvent + 22] = 0x83; bytes[processEvent + 23] = 0x78;
        bytes[processEvent + 24] = 0x20; bytes[processEvent + 25] = 0x41;
        bytes[processEvent + 26] = 0xC3;
        PutString(bytes, 0x700, "InventoryMenu");

        // IMAGE_RUNTIME_FUNCTION_ENTRY used by the conservative whole-DLL
        // CommonLib pass. The active-sink test above addresses the same code
        // directly, while the static pass intentionally walks only .pdata.
        Put(bytes, 0xA00, std::uint32_t{ 0x1080 });
        Put(bytes, 0xA04, std::uint32_t{ 0x109B });
        Put(bytes, 0xA08, std::uint32_t{ 0 });

        std::ofstream output(path, std::ios::binary | std::ios::trunc);
        output.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    }
}

int main(const int argc, char** argv)
{
    if (argc > 1) {
        for (int argument = 1; argument < argc; ++argument) {
            const std::filesystem::path dll(argv[argument]);
            UHI::Scanners::PeInputAnalyzer analyzer;
            auto records = analyzer.Scan(dll);
            auto staticHandlers = analyzer.ScanStaticInputHandlers(dll);
            records.insert(records.end(), staticHandlers.begin(), staticHandlers.end());
            std::cout << dll.string() << '\t' << records.size()
                      << " (static=" << staticHandlers.size() << ")\n";
            for (const auto& record : records) {
                std::cout << record.owner << '\t' << record.action << '\t' << record.binding << '\t'
                          << record.rawBinding << '\t' << record.codeSystem << "\n";
            }
        }
        return 0;
    }
    const auto root = std::filesystem::temp_directory_path() / "uhi_pe_input_test";
    std::error_code error;
    std::filesystem::remove_all(root, error);
    std::filesystem::create_directories(root);
    const auto dll = root / "NativeHotkeys.dll";
    MakePe(dll);
    const auto records = UHI::Scanners::PeInputAnalyzer{}.Scan(dll);
    const auto async = std::ranges::find_if(records, [](const auto& record) {
        return record.binding == "F10" && record.action.find("GetAsyncKeyState") != std::string::npos &&
            record.detector == "DllDisassemblyScanner" && !record.conflictEligible &&
            record.confidence == UHI::Confidence::candidate;
    });
    const auto registered = std::ranges::find_if(records, [](const auto& record) {
        return record.binding == "Ctrl+F10" && record.action == "NativeHotkeys" &&
            record.conflictEligible;
    });
    const std::vector activeTargets{
        UHI::Scanners::ActiveInputSinkTarget{ .modulePath = dll, .processEventRva = 0x1080 }
    };
    const auto active = UHI::Scanners::PeInputAnalyzer{}.ScanActiveInputSinks(activeTargets);
    const auto activeF7 = std::ranges::find_if(active, [](const auto& record) {
        return record.binding == "F7" && record.device == "keyboard" && record.runtimeActive &&
            record.detector == "ActiveInputSinkAnalyzer" && record.conflictEligible &&
            record.contextMask == static_cast<std::uint32_t>(UHI::ActivationContext::inventory) &&
            record.contextConfidence == UHI::ContextConfidence::inferred;
    });
    const auto staticHandlers = UHI::Scanners::PeInputAnalyzer{}.ScanStaticInputHandlers(dll);
    const auto staticF7 = std::ranges::find_if(staticHandlers, [](const auto& record) {
        return record.binding == "F7" && record.device == "keyboard" &&
            record.detector == "StaticCommonLibInputHandler" && !record.runtimeActive &&
            !record.conflictEligible && !record.editable;
    });
    {
        std::ofstream corrupt(root / "Corrupt.dll", std::ios::binary);
        corrupt << "MZbroken";
    }
    const auto rejected = UHI::Scanners::PeInputAnalyzer{}.Scan(root / "Corrupt.dll");
    std::filesystem::remove_all(root, error);
    if (async == records.end() || registered == records.end() || activeF7 == active.end() ||
        staticF7 == staticHandlers.end() ||
        !rejected.empty()) {
        std::cerr << "PE input analyzer test failed\n";
        return 1;
    }
    std::cout << "PE input analyzer test passed\n";
    return 0;
}
