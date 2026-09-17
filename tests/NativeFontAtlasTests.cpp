#include "UHI/NativeFontAtlas.h"
#include <iostream>

int main()
{
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
    }
    std::cout << "Windows UI font: " << fonts.systemDefaultUsed << ", atlas=" << atlas.TexWidth << 'x' << atlas.TexHeight << ", valid=" << ok << '\n';
    ImGui::DestroyContext();
    return ok ? 0 : 1;
}
