#include "UHI/NativeImGuiHost.h"

#include <RE/R/Renderer.h>
#include <SKSE/Logger.h>

#include <d3d11.h>
#include <dxgi.h>
#include <imgui.h>
#include <imgui_impl_dx11.h>
#include <imgui_impl_win32.h>

#include <array>
#include <atomic>
#include <filesystem>
#include <mutex>
#include <string>

namespace RE
{
    class GFxCharEvent : public RE::GFxEvent
    {
    public:
        GFxCharEvent() = default;

        explicit GFxCharEvent(std::uint32_t wcharCode, std::uint8_t keyboardIndex = 0) :
            GFxEvent(EventType::kCharEvent), wcharCode(wcharCode), keyboardIndex(keyboardIndex)
        {}

        std::uint32_t wcharCode{};
        std::uint32_t keyboardIndex{};
    };

    static_assert(sizeof(GFxCharEvent) == 0x0C);
}

namespace
{
    constexpr std::string_view kMenuName = "UniversalHotkeyManagerMenu";

    UHI::NativeImGuiHost::RenderCallback g_renderCallback{};
    std::atomic_bool g_registered{};
    std::atomic_bool g_open{};
    std::atomic_bool g_requestedOpen{};
    std::atomic_bool g_cursorOwnedByHost{};
    std::mutex g_rendererMutex;
    bool g_rendererInitialized{};
    std::string g_imguiIniPath;
    HWND g_outputWindow{};

    struct KeyMapping
    {
        RE::GFxKey::Code gfx;
        ImGuiKey imgui;
    };

    constexpr std::array<KeyMapping, 32> kKeyMappings{{
        { RE::GFxKey::kAlt, ImGuiMod_Alt },
        { RE::GFxKey::kControl, ImGuiMod_Ctrl },
        { RE::GFxKey::kShift, ImGuiMod_Shift },
        { RE::GFxKey::kCapsLock, ImGuiKey_CapsLock },
        { RE::GFxKey::kHome, ImGuiKey_Home },
        { RE::GFxKey::kEnd, ImGuiKey_End },
        { RE::GFxKey::kPageUp, ImGuiKey_PageUp },
        { RE::GFxKey::kPageDown, ImGuiKey_PageDown },
        { RE::GFxKey::kComma, ImGuiKey_Comma },
        { RE::GFxKey::kPeriod, ImGuiKey_Period },
        { RE::GFxKey::kSlash, ImGuiKey_Slash },
        { RE::GFxKey::kBackslash, ImGuiKey_Backslash },
        { RE::GFxKey::kQuote, ImGuiKey_Apostrophe },
        { RE::GFxKey::kBracketLeft, ImGuiKey_LeftBracket },
        { RE::GFxKey::kBracketRight, ImGuiKey_RightBracket },
        { RE::GFxKey::kTab, ImGuiKey_Tab },
        { RE::GFxKey::kReturn, ImGuiKey_Enter },
        { RE::GFxKey::kEqual, ImGuiKey_Equal },
        { RE::GFxKey::kMinus, ImGuiKey_Minus },
        { RE::GFxKey::kEscape, ImGuiKey_Escape },
        { RE::GFxKey::kLeft, ImGuiKey_LeftArrow },
        { RE::GFxKey::kUp, ImGuiKey_UpArrow },
        { RE::GFxKey::kRight, ImGuiKey_RightArrow },
        { RE::GFxKey::kDown, ImGuiKey_DownArrow },
        { RE::GFxKey::kSpace, ImGuiKey_Space },
        { RE::GFxKey::kBackspace, ImGuiKey_Backspace },
        { RE::GFxKey::kDelete, ImGuiKey_Delete },
        { RE::GFxKey::kInsert, ImGuiKey_Insert },
        { RE::GFxKey::kKP_Multiply, ImGuiKey_KeypadMultiply },
        { RE::GFxKey::kKP_Add, ImGuiKey_KeypadAdd },
        { RE::GFxKey::kKP_Enter, ImGuiKey_KeypadEnter },
        { RE::GFxKey::kKP_Subtract, ImGuiKey_KeypadSubtract }
    }};

    ImGuiKey ConvertKey(const RE::GFxKey::Code key)
    {
        if (key >= RE::GFxKey::kA && key <= RE::GFxKey::kZ) {
            return static_cast<ImGuiKey>(key - RE::GFxKey::kA + ImGuiKey_A);
        }
        if (key >= RE::GFxKey::kF1 && key <= RE::GFxKey::kF15) {
            return static_cast<ImGuiKey>(key - RE::GFxKey::kF1 + ImGuiKey_F1);
        }
        if (key >= RE::GFxKey::kNum0 && key <= RE::GFxKey::kNum9) {
            return static_cast<ImGuiKey>(key - RE::GFxKey::kNum0 + ImGuiKey_0);
        }
        if (key >= RE::GFxKey::kKP_0 && key <= RE::GFxKey::kKP_9) {
            return static_cast<ImGuiKey>(key - RE::GFxKey::kKP_0 + ImGuiKey_Keypad0);
        }
        for (const auto& [gfx, imgui] : kKeyMappings) {
            if (gfx == key) return imgui;
        }
        return ImGuiKey_None;
    }

    void AddMergedFont(ImGuiIO& io, const char* path, const ImWchar* ranges)
    {
        if (!std::filesystem::exists(std::filesystem::path(path))) return;
        ImFontConfig config{};
        config.MergeMode = true;
        config.PixelSnapH = true;
        config.OversampleH = 2;
        config.OversampleV = 2;
        io.Fonts->AddFontFromFileTTF(path, 32.0F, &config, ranges);
    }

    void BuildFontAtlas(ImGuiIO& io)
    {
        io.Fonts->Clear();
        ImFontConfig baseConfig{};
        baseConfig.OversampleH = 2;
        baseConfig.OversampleV = 2;
        constexpr auto* basePath = "C:\\Windows\\Fonts\\segoeui.ttf";
        if (std::filesystem::exists(std::filesystem::path(basePath))) {
            io.FontDefault = io.Fonts->AddFontFromFileTTF(
                basePath, 32.0F, &baseConfig, io.Fonts->GetGlyphRangesDefault());
        } else {
            io.FontDefault = io.Fonts->AddFontDefault();
            SKSE::log::warn("Segoe UI was not found; UHM is using Dear ImGui's fallback font");
        }

        AddMergedFont(io, "C:\\Windows\\Fonts\\malgun.ttf", io.Fonts->GetGlyphRangesKorean());
        AddMergedFont(io, "C:\\Windows\\Fonts\\msyh.ttc", io.Fonts->GetGlyphRangesChineseFull());
        AddMergedFont(io, "C:\\Windows\\Fonts\\YuGothR.ttc", io.Fonts->GetGlyphRangesJapanese());
    }

    ImVec4 HexColor(const std::uint32_t rgba)
    {
        return ImVec4(
            static_cast<float>((rgba >> 24U) & 0xFFU) / 255.0F,
            static_cast<float>((rgba >> 16U) & 0xFFU) / 255.0F,
            static_cast<float>((rgba >> 8U) & 0xFFU) / 255.0F,
            static_cast<float>(rgba & 0xFFU) / 255.0F);
    }

    void ApplyLegacyStyle()
    {
        auto& style = ImGui::GetStyle();
        style.WindowRounding = 0.0F;
        style.FrameRounding = 0.0F;
        style.GrabRounding = 2.0F;
        style.ScrollbarRounding = 2.0F;
        style.ChildRounding = 0.0F;
        style.PopupRounding = 0.0F;
        style.WindowBorderSize = 3.0F;
        style.ChildBorderSize = 3.0F;
        style.FrameBorderSize = 1.0F;
        style.PopupBorderSize = 3.0F;
        style.TabBarBorderSize = 3.0F;
        style.TabBorderSize = 3.0F;
        style.FramePadding = ImVec2(10.0F, 5.0F);

        auto* colors = style.Colors;
        colors[ImGuiCol_Text] = HexColor(0xFFFFFFFFU);
        colors[ImGuiCol_TextDisabled] = HexColor(0xFFFFFF33U);
        colors[ImGuiCol_WindowBg] = HexColor(0x00000099U);
        colors[ImGuiCol_ChildBg] = HexColor(0x00000000U);
        colors[ImGuiCol_PopupBg] = HexColor(0x00000099U);
        colors[ImGuiCol_Border] = HexColor(0xFFFFFF66U);
        colors[ImGuiCol_BorderShadow] = HexColor(0xFFFFFF33U);
        colors[ImGuiCol_FrameBg] = HexColor(0x00000000U);
        colors[ImGuiCol_FrameBgHovered] = HexColor(0xFFFFFF33U);
        colors[ImGuiCol_FrameBgActive] = HexColor(0xFFFFFF66U);
        colors[ImGuiCol_TitleBg] = HexColor(0x000000FFU);
        colors[ImGuiCol_TitleBgActive] = HexColor(0x000000FFU);
        colors[ImGuiCol_TitleBgCollapsed] = HexColor(0x000000FFU);
        colors[ImGuiCol_MenuBarBg] = HexColor(0x00000000U);
        colors[ImGuiCol_ScrollbarBg] = HexColor(0xFFFFFF33U);
        colors[ImGuiCol_ScrollbarGrab] = HexColor(0xFFFFFF33U);
        colors[ImGuiCol_ScrollbarGrabHovered] = HexColor(0xFFFFFF33U);
        colors[ImGuiCol_ScrollbarGrabActive] = HexColor(0xFFFFFF66U);
        colors[ImGuiCol_CheckMark] = HexColor(0xFFFFFFFFU);
        colors[ImGuiCol_SliderGrab] = HexColor(0xFFFFFF33U);
        colors[ImGuiCol_SliderGrabActive] = HexColor(0xFFFFFF33U);
        colors[ImGuiCol_Button] = HexColor(0x00000000U);
        colors[ImGuiCol_ButtonHovered] = HexColor(0xFFFFFF33U);
        colors[ImGuiCol_ButtonActive] = HexColor(0xFFFFFF66U);
        colors[ImGuiCol_Header] = HexColor(0xFFFFFF1AU);
        colors[ImGuiCol_HeaderHovered] = HexColor(0xFFFFFF33U);
        colors[ImGuiCol_HeaderActive] = HexColor(0xFFFFFF66U);
        colors[ImGuiCol_Separator] = HexColor(0xFFFFFF66U);
        colors[ImGuiCol_SeparatorHovered] = HexColor(0xFFFFFF33U);
        colors[ImGuiCol_SeparatorActive] = HexColor(0xFFFFFF66U);
        colors[ImGuiCol_ResizeGrip] = HexColor(0x00000000U);
        colors[ImGuiCol_ResizeGripHovered] = HexColor(0xFFFFFF33U);
        colors[ImGuiCol_ResizeGripActive] = HexColor(0xFFFFFF66U);
        colors[ImGuiCol_Tab] = HexColor(0xFFFFFF33U);
        colors[ImGuiCol_TabHovered] = HexColor(0xFFFFFF33U);
        colors[ImGuiCol_TabSelected] = HexColor(0xFFFFFF66U);
        colors[ImGuiCol_TabDimmed] = HexColor(0x00000000U);
        colors[ImGuiCol_TabDimmedSelected] = HexColor(0xFFFFFF66U);
        colors[ImGuiCol_TableHeaderBg] = HexColor(0x00000000U);
        colors[ImGuiCol_NavCursor] = HexColor(0x4397FAFFU);
        colors[ImGuiCol_NavWindowingHighlight] = HexColor(0xFFFFFF33U);
        colors[ImGuiCol_NavWindowingDimBg] = HexColor(0x00000099U);
        colors[ImGuiCol_ModalWindowDimBg] = HexColor(0x00000000U);
    }

    bool EnsureRenderer()
    {
        std::scoped_lock lock(g_rendererMutex);
        if (g_rendererInitialized) return true;

        auto* renderer = RE::BSGraphics::Renderer::GetSingleton();
        if (!renderer) {
            SKSE::log::error("Could not initialize UHM ImGui: Skyrim renderer is unavailable");
            return false;
        }
        const auto& runtime = renderer->GetRuntimeData();
        if (!runtime.renderWindows || !runtime.renderWindows->swapChain || !runtime.forwarder || !runtime.context) {
            SKSE::log::error("Could not initialize UHM ImGui: incomplete DirectX 11 runtime data");
            return false;
        }

        auto* swapChain = reinterpret_cast<IDXGISwapChain*>(runtime.renderWindows->swapChain);
        auto* device = reinterpret_cast<ID3D11Device*>(runtime.forwarder);
        auto* context = reinterpret_cast<ID3D11DeviceContext*>(runtime.context);
        DXGI_SWAP_CHAIN_DESC description{};
        if (FAILED(swapChain->GetDesc(&description)) || !description.OutputWindow) {
            SKSE::log::error("Could not initialize UHM ImGui: swap-chain window is unavailable");
            return false;
        }

        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        ApplyLegacyStyle();
        auto& io = ImGui::GetIO();
        g_imguiIniPath = (std::filesystem::current_path() /
            "Data/SKSE/Plugins/UniversalHotkeyManager/imgui.ini").string();
        io.IniFilename = g_imguiIniPath.c_str();
        io.LogFilename = nullptr;
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_NavEnableGamepad;
        io.ConfigWindowsMoveFromTitleBarOnly = true;
        BuildFontAtlas(io);
        g_outputWindow = description.OutputWindow;

        if (!ImGui_ImplWin32_Init(description.OutputWindow) || !ImGui_ImplDX11_Init(device, context)) {
            SKSE::log::error("Could not initialize UHM ImGui Win32/DX11 backends");
            ImGui::DestroyContext();
            return false;
        }
        g_rendererInitialized = true;
        SKSE::log::info("UHM native Dear ImGui Win32/DX11 host initialized");
        return true;
    }

    void UpdateMousePosition()
    {
        if (!ImGui::GetCurrentContext()) return;
        auto& io = ImGui::GetIO();
        if (auto* ui = RE::UI::GetSingleton(); ui && ui->IsMenuOpen(RE::CursorMenu::MENU_NAME)) {
            if (const auto* cursor = RE::MenuCursor::GetSingleton()) {
                io.AddMouseSourceEvent(ImGuiMouseSource_Mouse);
                io.AddMousePosEvent(cursor->cursorPosX, cursor->cursorPosY);
                return;
            }
        }
        POINT point{};
        if (GetCursorPos(&point) && g_outputWindow && ScreenToClient(g_outputWindow, &point)) {
            io.AddMouseSourceEvent(ImGuiMouseSource_Mouse);
            io.AddMousePosEvent(static_cast<float>(point.x), static_cast<float>(point.y));
        }
    }

    void ProcessScaleformEvent(const RE::BSUIScaleformData* data)
    {
        if (!data || !data->scaleformEvent || !ImGui::GetCurrentContext()) return;
        const auto* event = data->scaleformEvent;
        auto& io = ImGui::GetIO();
        switch (event->type.get()) {
        case RE::GFxEvent::EventType::kMouseDown:
        case RE::GFxEvent::EventType::kMouseUp: {
            const auto* mouse = reinterpret_cast<const RE::GFxMouseEvent*>(event);
            io.AddMouseSourceEvent(ImGuiMouseSource_Mouse);
            io.AddMouseButtonEvent(static_cast<int>(mouse->button), event->type == RE::GFxEvent::EventType::kMouseDown);
            break;
        }
        case RE::GFxEvent::EventType::kMouseWheel: {
            const auto* mouse = reinterpret_cast<const RE::GFxMouseEvent*>(event);
            io.AddMouseWheelEvent(0.0F, mouse->scrollDelta);
            break;
        }
        case RE::GFxEvent::EventType::kKeyDown:
        case RE::GFxEvent::EventType::kKeyUp: {
            const auto* key = reinterpret_cast<const RE::GFxKeyEvent*>(event);
            const auto converted = ConvertKey(key->keyCode);
            if (converted != ImGuiKey_None) {
                io.AddKeyEvent(converted, event->type == RE::GFxEvent::EventType::kKeyDown);
            }
            break;
        }
        case RE::GFxEvent::EventType::kCharEvent: {
            const auto* character = reinterpret_cast<const RE::GFxCharEvent*>(event);
            if (io.WantTextInput && character->wcharCode >= 0x20U && character->wcharCode != 0x7FU) {
                io.AddInputCharacter(character->wcharCode);
            }
            break;
        }
        default:
            break;
        }
    }

    class NativeMenu final : public RE::IMenu
    {
    public:
        void PostDisplay() override
        {
            if (!EnsureRenderer()) return;
            ImGui_ImplWin32_NewFrame();
            ImGui_ImplDX11_NewFrame();
            UpdateMousePosition();
            ImGui::NewFrame();
            if (g_renderCallback) g_renderCallback();
            ImGui::Render();
            ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
            ForceCursor();
        }

        RE::UI_MESSAGE_RESULTS ProcessMessage(RE::UIMessage& message) override
        {
            switch (message.type.get()) {
            case RE::UI_MESSAGE_TYPE::kShow:
                g_open = true;
                g_requestedOpen = true;
                break;
            case RE::UI_MESSAGE_TYPE::kHide:
                g_open = false;
                g_requestedOpen = false;
                ReleaseCursor();
                break;
            case RE::UI_MESSAGE_TYPE::kScaleformEvent:
                ProcessScaleformEvent(reinterpret_cast<const RE::BSUIScaleformData*>(message.data));
                return RE::UI_MESSAGE_RESULTS::kHandled;
            default:
                break;
            }
            return IMenu::ProcessMessage(message);
        }

        static RE::IMenu* Creator()
        {
            using Flags = RE::UI_MENU_FLAGS;
            auto* menu = new NativeMenu();
            // UHM always owns the cursor while visible, including when a gamepad is
            // connected. kUpdateUsesCursor would clear kUsesCursor in that case.
            menu->menuFlags.set(Flags::kUsesCursor, Flags::kDontHideCursorWhenTopmost,
                Flags::kCustomRendering, Flags::kUsesMenuContext, Flags::kPausesGame,
                Flags::kModal, Flags::kDisablePauseMenu, Flags::kTopmostRenderedMenu,
                Flags::kRequiresUpdate);
            menu->depthPriority = 11;
            menu->inputContext.set(Context::kMenuMode);
            return menu;
        }

    private:
        static void ReleaseCursor()
        {
            if (!g_cursorOwnedByHost.exchange(false)) return;
            if (auto* tasks = SKSE::GetTaskInterface()) {
                tasks->AddUITask([] {
                    if (auto* queue = RE::UIMessageQueue::GetSingleton()) {
                        queue->AddMessage(RE::CursorMenu::MENU_NAME, RE::UI_MESSAGE_TYPE::kHide, nullptr);
                    }
                });
            }
        }

        static void ForceCursor()
        {
            auto* ui = RE::UI::GetSingleton();
            if (!ui || ui->IsMenuOpen(RE::CursorMenu::MENU_NAME)) return;
            if (g_cursorOwnedByHost.exchange(true)) return;
            if (auto* tasks = SKSE::GetTaskInterface()) {
                tasks->AddUITask([] {
                    if (auto* queue = RE::UIMessageQueue::GetSingleton()) {
                        queue->AddMessage(RE::CursorMenu::MENU_NAME, RE::UI_MESSAGE_TYPE::kShow, nullptr);
                    }
                });
            }
        }
    };

    void QueueVisibility(const bool open)
    {
        g_requestedOpen = open;
        if (auto* tasks = SKSE::GetTaskInterface()) {
            tasks->AddUITask([open] {
                if (auto* queue = RE::UIMessageQueue::GetSingleton()) {
                    queue->AddMessage(kMenuName, open ? RE::UI_MESSAGE_TYPE::kShow : RE::UI_MESSAGE_TYPE::kHide, nullptr);
                }
            });
        }
    }
}

namespace UHI::NativeImGuiHost
{
    bool Register(const RenderCallback callback)
    {
        g_renderCallback = callback;
        if (g_registered.exchange(true)) return true;
        if (auto* ui = RE::UI::GetSingleton()) {
            ui->Register(kMenuName, NativeMenu::Creator);
            SKSE::log::info("Registered UHM native IMenu host");
            return true;
        }
        g_registered = false;
        SKSE::log::error("Could not register UHM native IMenu host: UI singleton unavailable");
        return false;
    }

    bool Toggle()
    {
        if (!g_registered.load()) return false;
        QueueVisibility(!g_requestedOpen.load());
        return true;
    }

    bool Close()
    {
        if (!g_registered.load() || (!g_requestedOpen.load() && !g_open.load())) return false;
        QueueVisibility(false);
        return true;
    }

    bool IsOpen() noexcept
    {
        return g_requestedOpen.load() || g_open.load();
    }
}
