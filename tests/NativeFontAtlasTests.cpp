#include "UHI/NativeFontAtlas.h"
#include <iostream>
#include <fstream>
#include <set>
#include <cstdlib>
#include <cstddef>

namespace
{
    struct alignas(std::max_align_t) Allocation { std::size_t bytes; };
    std::size_t liveBytes{}, liveBlocks{};
    void* Allocate(std::size_t size, void*)
    {
        auto* block = static_cast<Allocation*>(std::malloc(sizeof(Allocation) + size));
        if (!block) return nullptr;
        block->bytes = size;
        liveBytes += size;
        ++liveBlocks;
        return block + 1;
    }
    void Free(void* pointer, void*)
    {
        if (!pointer) return;
        auto* block = static_cast<Allocation*>(pointer) - 1;
        liveBytes -= block->bytes;
        --liveBlocks;
        std::free(block);
    }

    bool CheckCollectionDirectory()
    {
        std::array<unsigned char, 128> file{};
        const auto write = [&](std::size_t offset, std::uint32_t value) {
            for (unsigned i = 0; i < 4; ++i) file[offset + i] = static_cast<unsigned char>(value >> (24 - i * 8));
        };
        write(0, 0x74746366); write(4, 0x00010000); write(8, 2);
        write(12, 24); write(16, 56);
        for (const auto offset : {24U, 56U}) {
            write(offset, 0x00010000); file[offset + 5] = 1;
            write(offset + 12, 0x6E616D65); // name table, different for each face
            write(offset + 20, offset + 72); write(offset + 24, 0);
        }
        const auto first = std::span<const unsigned char>(file).subspan(24, 28);
        const auto second = std::span<const unsigned char>(file).subspan(56, 28);
        using UHI::NativeFontDetail::FindSelectedFace;
        bool ok = FindSelectedFace(file, first) == 0 && FindSelectedFace(file, second) == 1;
        // The old table=0 slice retains collection-relative offsets.
        ok = !FindSelectedFace(std::span<const unsigned char>(file).subspan(56), second) && ok;
        ok = !FindSelectedFace(file, second.first(12)) && ok;
        auto invalid = file;
        invalid[16] = 0xFF; // out-of-file second face
        ok = !FindSelectedFace(invalid, second) && ok;
        invalid = file;
        invalid[4] = 0xFF; // unsupported TTC version
        ok = !FindSelectedFace(invalid, second) && ok;
        invalid = file;
        invalid[8] = 0xFF; // oversized face count
        ok = !FindSelectedFace(invalid, second) && ok;
        std::cout << "TTC face identity and truncated/invalid directory checks: " << ok << '\n';
        return ok;
    }

    bool CheckWindowsFont(const wchar_t* face, bool collection)
    {
        LOGFONTW logical{};
        logical.lfHeight = -16;
        wcsncpy_s(logical.lfFaceName, face, _TRUNCATE);
        const UHI::NativeFontDetail::GdiFontSelection selected(logical);
        if (!selected.previous || selected.previous == HGDI_ERROR) return false;
        // Independently compare the chosen face's name table with Windows' live
        // selection. Merely loading TTC face zero would render the wrong font.
        const auto nameSize = GetFontData(selected.dc, 0x656D616E, 0, nullptr, 0);
        if (nameSize == GDI_ERROR || nameSize > 1024U * 1024U) return false;
        std::vector<unsigned char> expected(nameSize);
        if (GetFontData(selected.dc, 0x656D616E, 0, expected.data(), nameSize) != nameSize) return false;
        const auto beforeBytes = liveBytes, beforeBlocks = liveBlocks;
        const auto beforeGdi = GetGuiResources(GetCurrentProcess(), GR_GDIOBJECTS);
        bool ok = true;
        int faceIndex = -1;
        for (int cycle = 0; cycle < 5; ++cycle) {
            ImGui::CreateContext();
            auto& atlas = *ImGui::GetIO().Fonts;
            auto* font = UHI::AddWindowsLogicalFont(atlas, logical);
            bool nameMatches = false;
            if (font && atlas.Sources.Size == 1) {
                const auto& config = atlas.Sources[0];
                faceIndex = config.FontNo;
                const auto* bytes = static_cast<const unsigned char*>(config.FontData);
                const auto read = UHI::NativeFontDetail::Read32;
                const bool isCollection = read(bytes) == 0x74746366;
                ok = ok && isCollection == collection;
                const auto offset = isCollection ? read(bytes + 12 + config.FontNo * 4) : 0U;
                const auto count = (unsigned{bytes[offset + 4]} << 8) | bytes[offset + 5];
                for (unsigned i = 0; i < count; ++i) {
                    const auto* table = bytes + offset + 12 + i * 16;
                    if (read(table) == 0x6E616D65 && read(table + 12) == nameSize)
                        nameMatches = std::memcmp(bytes + read(table + 8), expected.data(), nameSize) == 0;
                }
            }
            ok = font && nameMatches && atlas.Build() && ok;
            if (font) for (const auto code : {'A', '0', '+'})
                ok = font->FindGlyphNoFallback(static_cast<ImWchar>(code)) && ok;
            ImGui::DestroyContext();
            ok = ok && liveBytes == beforeBytes && liveBlocks == beforeBlocks;
            ok = ok && GetGuiResources(GetCurrentProcess(), GR_GDIOBJECTS) == beforeGdi;
        }
        std::wcout << L"Windows face " << face << L", TTC=" << collection << L", index=" << faceIndex
            << L", 5 build/free cycles=" << ok << L'\n';
        return ok;
    }
}

int main()
{
    ImGui::SetAllocatorFunctions(Allocate, Free);
    ImGui::CreateContext();
    auto& atlas = *ImGui::GetIO().Fonts;
    const auto fonts = UHI::BuildNativeFontAtlas(atlas);
    bool ok = fonts.valid && fonts.standard && fonts.systemDefaultUsed;
    if (const auto font = fonts.standard) {
        for (const auto code : { 'A', '0', '+' }) ok = ok && font->FindGlyphNoFallback(static_cast<ImWchar>(code));
        const auto directory = UHI::WindowsFontDirectory();
        if (std::filesystem::exists(directory / L"malgun.ttf")) ok = ok && font->FindGlyphNoFallback(0xD55C);
        if (std::filesystem::exists(directory / L"msyh.ttc") || std::filesystem::exists(directory / L"msjh.ttc"))
            ok = ok && font->FindGlyphNoFallback(0x4E2D);
        const bool chinese = std::filesystem::exists(directory / L"msyh.ttc") || std::filesystem::exists(directory / L"msjh.ttc");
        const bool korean = std::filesystem::exists(directory / L"malgun.ttf");
        std::ifstream source(UHI_UI_SOURCE, std::ios::binary);
        const std::string text((std::istreambuf_iterator<char>(source)), {});
        ok = ok && !text.empty();
        std::set<unsigned> checked;
        for (std::size_t i = 0; i + 2 < text.size(); ++i) {
            const auto lead = static_cast<unsigned char>(text[i]);
            if ((lead & 0xF0) != 0xE0) continue;
            const auto code = ((lead & 15U) << 12) | ((static_cast<unsigned char>(text[i + 1]) & 63U) << 6) |
                (static_cast<unsigned char>(text[i + 2]) & 63U);
            if (((chinese && code >= 0x4E00 && code <= 0x9FFF) || (korean && code >= 0xAC00 && code <= 0xD7A3)) &&
                checked.insert(code).second && !font->FindGlyphNoFallback(static_cast<ImWchar>(code))) {
                std::cerr << "Missing UI glyph U+" << std::hex << code << std::dec << '\n';
                ok = false;
            }
            i += 2;
        }
    }
    std::cout << "Windows UI font: " << fonts.systemDefaultUsed << ", atlas=" << atlas.TexWidth << 'x' << atlas.TexHeight << ", valid=" << ok << '\n';
    ImGui::DestroyContext();
    // ImGui's static glyph-range cache intentionally survives a context. After
    // that one-time warmup, repeated font/context teardown must retain no more.
    const auto retainedBytes = liveBytes;
    const auto retainedBlocks = liveBlocks;
    for (int cycle = 0; cycle < 20; ++cycle) {
        ImGui::CreateContext();
        ok = UHI::BuildNativeFontAtlas(*ImGui::GetIO().Fonts).valid && ok;
        ImGui::DestroyContext();
        ok = ok && liveBytes == retainedBytes && liveBlocks == retainedBlocks;
    }
    std::cout << "20 font/context cycles: retained bytes=" << liveBytes
              << ", baseline=" << retainedBytes << ", blocks=" << liveBlocks << '\n';
    ok = CheckCollectionDirectory() && ok;
    const auto directory = UHI::WindowsFontDirectory();
    struct FontCase { const wchar_t* file; const wchar_t* face; bool collection; };
    for (const auto& test : {
             FontCase{L"segoeui.ttf", L"Segoe UI", false},
             FontCase{L"malgun.ttf", L"Malgun Gothic", false},
             FontCase{L"msyh.ttc", L"Microsoft YaHei", true},
             FontCase{L"msyh.ttc", L"Microsoft YaHei UI", true},
             FontCase{L"msjh.ttc", L"Microsoft JhengHei", true},
             FontCase{L"msjh.ttc", L"Microsoft JhengHei UI", true},
             FontCase{L"msgothic.ttc", L"MS Gothic", true},
             FontCase{L"msgothic.ttc", L"MS UI Gothic", true},
             FontCase{L"YuGothR.ttc", L"Yu Gothic UI", true}}) {
        if (std::filesystem::exists(directory / test.file)) ok = CheckWindowsFont(test.face, test.collection) && ok;
        else std::wcout << L"Font not installed, skipped: " << test.face << L'\n';
    }
    return ok ? 0 : 1;
}
