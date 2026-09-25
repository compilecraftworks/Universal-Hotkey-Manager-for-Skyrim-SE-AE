#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <imgui.h>
#include <array>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <vector>

namespace UHI
{
    struct NativeFonts { ImFont* standard{}; bool systemDefaultUsed{}; bool valid{}; };

    inline std::filesystem::path WindowsFontDirectory()
    {
        wchar_t directory[MAX_PATH]{};
        const auto size = GetWindowsDirectoryW(directory, MAX_PATH);
        return size && size < MAX_PATH ? std::filesystem::path(directory) / L"Fonts" : std::filesystem::path{};
    }

    namespace NativeFontDetail
    {
        inline std::uint32_t Read32(const unsigned char* bytes) noexcept
        {
            return (std::uint32_t{bytes[0]} << 24) | (std::uint32_t{bytes[1]} << 16) |
                (std::uint32_t{bytes[2]} << 8) | bytes[3];
        }

        // SFNT table offsets are relative to the complete file, including for
        // faces inside a TTC. Check the directory before passing it to stb.
        inline bool ValidDirectory(std::span<const unsigned char> file, std::size_t offset) noexcept
        {
            if (offset > file.size() || file.size() - offset < 12) return false;
            const auto* header = file.data() + offset;
            const auto signature = Read32(header);
            if (signature != 0x00010000 && signature != 0x4F54544F &&
                signature != 0x74727565 && signature != 0x31000000) return false;
            const auto count = (std::uint32_t{header[4]} << 8) | header[5];
            if (!count || count > (file.size() - offset - 12) / 16) return false;
            for (std::uint32_t i = 0; i < count; ++i) {
                const auto* table = header + 12 + i * 16;
                const auto start = Read32(table + 8), length = Read32(table + 12);
                if (start > file.size() || length > file.size() - start) return false;
            }
            return true;
        }

        inline std::optional<int> FindSelectedFace(std::span<const unsigned char> file,
            std::span<const unsigned char> selectedDirectory) noexcept
        {
            if (file.size() < 12 || selectedDirectory.size() < 12) return std::nullopt;
            const auto selectedCount = (std::uint32_t{selectedDirectory[4]} << 8) | selectedDirectory[5];
            if (!selectedCount || selectedDirectory.size() != 12U + selectedCount * 16U) return std::nullopt;
            const auto matches = [&](const std::size_t offset) {
                return ValidDirectory(file, offset) && selectedDirectory.size() <= file.size() - offset &&
                    std::memcmp(file.data() + offset, selectedDirectory.data(), selectedDirectory.size()) == 0;
            };
            if (Read32(file.data()) != 0x74746366) return matches(0) ? std::optional<int>{0} : std::nullopt;
            const auto version = Read32(file.data() + 4), count = Read32(file.data() + 8);
            if ((version != 0x00010000 && version != 0x00020000) || count > (file.size() - 12) / 4)
                return std::nullopt;
            for (std::uint32_t i = 0; i < count; ++i)
                if (matches(Read32(file.data() + 12 + i * 4))) return static_cast<int>(i);
            return std::nullopt;
        }

        struct GdiFontSelection
        {
            HFONT font{};
            HDC dc{};
            HGDIOBJ previous{};
            explicit GdiFontSelection(const LOGFONTW& logicalFont)
            {
                font = CreateFontIndirectW(&logicalFont);
                if (font) dc = CreateCompatibleDC(nullptr);
                if (dc) previous = SelectObject(dc, font);
            }
            ~GdiFontSelection()
            {
                if (previous && previous != HGDI_ERROR) SelectObject(dc, previous);
                if (dc) DeleteDC(dc);
                if (font) DeleteObject(font);
            }
            GdiFontSelection(const GdiFontSelection&) = delete;
            GdiFontSelection& operator=(const GdiFontSelection&) = delete;
        };
    }

    inline ImFont* AddWindowsLogicalFont(ImFontAtlas& atlas, const LOGFONTW& logicalFont)
    {
        const NativeFontDetail::GdiFontSelection selected(logicalFont);
        if (!selected.previous || selected.previous == HGDI_ERROR) return nullptr;
        // GetFontData(table=0) starts at the selected TTC face, but its table
        // offsets still refer to the beginning of the collection. Read 'ttcf'
        // first so ImGui receives the complete file, then select the same face.
        // https://learn.microsoft.com/windows/win32/api/wingdi/nf-wingdi-getfontdata
        DWORD table = 0x66637474;
        auto size = GetFontData(selected.dc, table, 0, nullptr, 0);
        if (size == GDI_ERROR) { table = 0; size = GetFontData(selected.dc, table, 0, nullptr, 0); }
        if (size == GDI_ERROR || size < 12 || size > 32U * 1024U * 1024U) return nullptr;
        std::unique_ptr<void, decltype(&ImGui::MemFree)> data(ImGui::MemAlloc(size), &ImGui::MemFree);
        if (!data || GetFontData(selected.dc, table, 0, data.get(), size) != size) return nullptr;
        std::array<unsigned char, 12> header{};
        if (GetFontData(selected.dc, 0, 0, header.data(), static_cast<DWORD>(header.size())) != header.size())
            return nullptr;
        const auto count = (std::uint32_t{header[4]} << 8) | header[5];
        const auto directorySize = 12U + count * 16U;
        if (directorySize > size) return nullptr;
        std::vector<unsigned char> directory(directorySize);
        if (GetFontData(selected.dc, 0, 0, directory.data(), directorySize) != directorySize) return nullptr;
        const auto face = NativeFontDetail::FindSelectedFace(
            {static_cast<const unsigned char*>(data.get()), size}, directory);
        if (!face) return nullptr;
        ImFontConfig config{};
        config.FontNo = *face;
        config.PixelSnapH = true;
        config.OversampleH = config.OversampleV = 1;
        // ImGui owns this copy; no Windows font file is changed or redistributed.
        auto* font = atlas.AddFontFromMemoryTTF(data.get(), static_cast<int>(size), 32.0F, &config, atlas.GetGlyphRangesDefault());
        if (font) data.release();
        return font;
    }

    inline ImFont* AddWindowsUiFont(ImFontAtlas& atlas)
    {
        NONCLIENTMETRICSW metrics{};
        metrics.cbSize = sizeof(metrics);
        if (!SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(metrics), &metrics, 0)) return nullptr;
        return AddWindowsLogicalFont(atlas, metrics.lfMessageFont);
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
