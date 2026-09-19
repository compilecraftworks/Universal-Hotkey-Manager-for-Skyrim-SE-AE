#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <imgui.h>
#include <filesystem>

namespace UHI
{
    struct NativeFonts { ImFont* standard{}; bool systemDefaultUsed{}; bool valid{}; };

    inline std::filesystem::path WindowsFontDirectory()
    {
        wchar_t directory[MAX_PATH]{};
        const auto size = GetWindowsDirectoryW(directory, MAX_PATH);
        return size && size < MAX_PATH ? std::filesystem::path(directory) / L"Fonts" : std::filesystem::path{};
    }

    inline ImFont* AddWindowsUiFont(ImFontAtlas& atlas)
    {
        NONCLIENTMETRICSW metrics{};
        metrics.cbSize = sizeof(metrics);
        if (!SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(metrics), &metrics, 0)) return nullptr;
        const auto font = CreateFontIndirectW(&metrics.lfMessageFont);
        if (!font) return nullptr;
        const auto dc = CreateCompatibleDC(nullptr);
        if (!dc) { DeleteObject(font); return nullptr; }
        const auto previous = SelectObject(dc, font);
        const auto size = GetFontData(dc, 0, 0, nullptr, 0);
        void* data{};
        bool loaded{};
        if (size != GDI_ERROR && size > 0 && size <= 32U * 1024U * 1024U) {
            data = ImGui::MemAlloc(size);
            loaded = data && GetFontData(dc, 0, 0, data, size) == size;
        }
        SelectObject(dc, previous);
        DeleteDC(dc);
        DeleteObject(font);
        if (!loaded) { if (data) ImGui::MemFree(data); return nullptr; }
        ImFontConfig config{};
        config.PixelSnapH = true;
        config.OversampleH = config.OversampleV = 1;
        // ImGui owns this copy; no Windows font file is changed or redistributed.
        return atlas.AddFontFromMemoryTTF(data, static_cast<int>(size), 32.0F, &config, atlas.GetGlyphRangesDefault());
    }

    inline NativeFonts BuildNativeFontAtlas(ImFontAtlas& atlas)
    {
        atlas.Clear();
        atlas.TexDesiredWidth = 4096;
        const auto directory = WindowsFontDirectory();
        const auto add = [&](const wchar_t* filename, const ImWchar* ranges, bool merge) -> ImFont* {
            if (directory.empty()) return nullptr;
            const auto path = directory / filename;
            std::error_code error;
            if (!std::filesystem::is_regular_file(path, error)) return nullptr;
            ImFontConfig config{};
            config.MergeMode = merge;
            config.PixelSnapH = true;
            config.OversampleH = config.OversampleV = 1;
            const auto utf8 = path.u8string();
            return atlas.AddFontFromFileTTF(reinterpret_cast<const char*>(utf8.c_str()), 32.0F, &config, ranges);
        };
        NativeFonts result;
        result.standard = AddWindowsUiFont(atlas);
        result.systemDefaultUsed = result.standard != nullptr;
        if (!result.standard) result.standard = add(L"segoeui.ttf", atlas.GetGlyphRangesDefault(), false);
        if (!result.standard) result.standard = atlas.AddFontDefault();
        add(L"malgun.ttf", atlas.GetGlyphRangesKorean(), true);
        // Keep the existing compact atlas and add the UI's three missing
        // characters. The ranges must outlive ImGui's deferred atlas builds.
        static const auto chineseRanges = [] {
            ImFontGlyphRangesBuilder builder;
            builder.AddRanges(ImGui::GetIO().Fonts->GetGlyphRangesChineseSimplifiedCommon());
            builder.AddText("拟浏辑");
            ImVector<ImWchar> ranges;
            builder.BuildRanges(&ranges);
            return ranges;
        }();
        add(L"msyh.ttc", chineseRanges.Data, true);
        add(L"msjh.ttc", chineseRanges.Data, true);
        result.valid = atlas.Build() && atlas.TexWidth > 0 && atlas.TexHeight > 0 &&
            atlas.TexWidth <= 16384 && atlas.TexHeight <= 16384;
        return result;
    }
}
