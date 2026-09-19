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
    return ok ? 0 : 1;
}
