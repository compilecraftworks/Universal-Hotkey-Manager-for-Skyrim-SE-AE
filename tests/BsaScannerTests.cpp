#include "UHI/scanners/BsaScanner.h"

#include <lz4.h>
#include <zlib.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{
    template <class T>
    void Write(std::ofstream& output, const T value)
    {
        output.write(reinterpret_cast<const char*>(&value), sizeof(value));
    }

    std::vector<char> Pack(const std::string& content, const std::uint32_t version, const bool compressed)
    {
        if (!compressed) return { content.begin(), content.end() };
        std::vector<char> result(sizeof(std::uint32_t));
        const auto unpackedSize = static_cast<std::uint32_t>(content.size());
        std::memcpy(result.data(), &unpackedSize, sizeof(unpackedSize));
        if (version == 105) {
            std::vector<char> packed(static_cast<std::size_t>(LZ4_compressBound(static_cast<int>(content.size()))));
            const auto size = LZ4_compress_default(content.data(), packed.data(),
                static_cast<int>(content.size()), static_cast<int>(packed.size()));
            if (size <= 0) throw std::runtime_error("LZ4 test compression failed");
            result.insert(result.end(), packed.begin(), packed.begin() + size);
        } else {
            uLongf size = compressBound(static_cast<uLong>(content.size()));
            std::vector<char> packed(size);
            if (compress2(reinterpret_cast<Bytef*>(packed.data()), &size,
                    reinterpret_cast<const Bytef*>(content.data()), static_cast<uLong>(content.size()),
                    Z_BEST_SPEED) != Z_OK) {
                throw std::runtime_error("zlib test compression failed");
            }
            result.insert(result.end(), packed.begin(), packed.begin() + static_cast<std::ptrdiff_t>(size));
        }
        return result;
    }

    void MakeArchive(const std::filesystem::path& path, const std::uint32_t version, const bool compressed)
    {
        constexpr std::array<char, 4> magic{ 'B', 'S', 'A', '\0' };
        constexpr std::uint32_t headerSize = 36;
        const std::string folder = "SKSE\\Plugins";
        const std::array<std::string, 2> names{ "settings.ini", "script.psc" };
        const std::array<std::string, 2> contents{
            "ScanCode=68\n", "Event OnInit() RegisterForKey(69) EndEvent\n" };
        std::array<std::vector<char>, 2> stored{
            Pack(contents[0], version, compressed), Pack(contents[1], version, compressed)
        };
        const std::uint32_t folderRecordSize = version == 105 ? 24U : 16U;
        const std::uint32_t folderBlockSize = 1U + static_cast<std::uint32_t>(folder.size()) + 1U + 32U;
        std::uint32_t fileNameBytes{};
        for (const auto& name : names) fileNameBytes += static_cast<std::uint32_t>(name.size()) + 1U;
        const std::uint32_t firstDataOffset = headerSize + folderRecordSize + folderBlockSize + fileNameBytes;
        const std::array<std::uint32_t, 2> offsets{
            firstDataOffset, firstDataOffset + static_cast<std::uint32_t>(stored[0].size())
        };

        std::ofstream output(path, std::ios::binary | std::ios::trunc);
        output.write(magic.data(), magic.size());
        Write(output, version);
        Write(output, headerSize);
        Write(output, compressed ? 0x0007U : 0x0003U);
        Write(output, 1U);
        Write(output, 2U);
        Write(output, static_cast<std::uint32_t>(folder.size()) + 1U);
        Write(output, fileNameBytes);
        Write(output, 0U);

        Write(output, std::uint64_t{});
        Write(output, 2U);
        if (version == 105) {
            Write(output, 0U);
            Write(output, std::uint64_t{});
        } else {
            Write(output, 0U);
        }

        Write(output, static_cast<std::uint8_t>(folder.size() + 1U));
        output.write(folder.data(), static_cast<std::streamsize>(folder.size()));
        output.put('\0');
        for (std::size_t index = 0; index < names.size(); ++index) {
            Write(output, std::uint64_t{});
            Write(output, static_cast<std::uint32_t>(stored[index].size()));
            Write(output, offsets[index]);
        }
        for (const auto& name : names) {
            output.write(name.data(), static_cast<std::streamsize>(name.size()));
            output.put('\0');
        }
        for (const auto& data : stored) output.write(data.data(), static_cast<std::streamsize>(data.size()));
    }

    bool HasRecord(const std::vector<UHI::HotkeyRecord>& records, const std::string_view binding,
        const UHI::ScanStage stage)
    {
        return std::ranges::any_of(records, [&](const auto& record) {
            return record.binding == binding && record.stage == stage &&
                record.evidencePath.string().find(".bsa::") != std::string::npos;
        });
    }
}

int main()
{
    const auto root = std::filesystem::temp_directory_path() / "uhi_bsa_scanner_test";
    std::error_code error;
    std::filesystem::remove_all(root, error);
    std::filesystem::create_directories(root);
    MakeArchive(root / "v104-uncompressed.bsa", 104, false);
    MakeArchive(root / "v104-zlib.bsa", 104, true);
    MakeArchive(root / "v105-uncompressed.bsa", 105, false);
    MakeArchive(root / "v105-lz4.bsa", 105, true);
    MakeArchive(root / "Skyrim - Interface.bsa", 104, false);
    MakeArchive(root / "Dawnguard.bsa", 104, false);
    MakeArchive(root / "ccBGSSSE001-Fish.bsa", 104, false);
    MakeArchive(root / "MarketplaceTextures.bsa", 104, false);
    MakeArchive(root / "_ResourcePack.bsa", 104, false);
    {
        std::ofstream corrupt(root / "corrupt.bsa", std::ios::binary);
        corrupt << "BSA\0\x69\0\0\0malformed";
    }

    const auto config = UHI::Scanners::BsaScanner{}.Scan(root, UHI::ScanStage::configuration);
    const auto scripts = UHI::Scanners::BsaScanner{}.Scan(root, UHI::ScanStage::scripts);
    const auto combined = UHI::Scanners::BsaScanner{}.ScanAll(root);
    const auto extracted = UHI::Scanners::BsaScanner{}.ExtractText(root / "v104-uncompressed.bsa",
        "SKSE/Plugins/settings.ini");
    const auto rejectedOfficial = UHI::Scanners::BsaScanner{}.ExtractText(root / "Skyrim - Interface.bsa",
        "SKSE/Plugins/settings.ini");
    const auto touchedOfficial = std::ranges::any_of(config, [](const auto& record) {
        const auto value = record.evidencePath.string();
        return value.find("Skyrim - Interface.bsa") != std::string::npos ||
            value.find("Dawnguard.bsa") != std::string::npos || value.find("ccBGSSSE001-Fish.bsa") != std::string::npos ||
            value.find("MarketplaceTextures.bsa") != std::string::npos || value.find("_ResourcePack.bsa") != std::string::npos;
    });
    std::filesystem::remove_all(root, error);
    if (!HasRecord(config, "F10", UHI::ScanStage::configuration) ||
        !HasRecord(scripts, "NumLk", UHI::ScanStage::scripts) || !extracted || rejectedOfficial || touchedOfficial ||
        !HasRecord(combined, "F10", UHI::ScanStage::configuration) ||
        !HasRecord(combined, "NumLk", UHI::ScanStage::scripts) ||
        extracted->find("ScanCode") == std::string::npos) {
        std::cerr << "BSA 104/105 extraction test failed\n";
        return 1;
    }
    std::cout << "BSA scanner test passed\n";
    return 0;
}
