#include "UHI/NativeImGuiHost.h"
#include "UHI/SkyrimRuntimeLayout.h"
#include "UHI/OpeningHotkey.h"
#include "UHI/NativeFontAtlas.h"
#include "UHI/NativeMenuLifecycle.h"
#include "UHI/NativeMenuPopupReset.h"

#include <RE/R/Renderer.h>
#include <REL/Relocation.h>
#include <SKSE/Logger.h>
#include <SKSE/SKSE.h>

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
#include <vector>

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
    UHI::NativeImGuiHost::CloseCallback g_closeCallback{};
    std::atomic_bool g_registered{};
    std::atomic_bool g_rendererHookInstalled{};
    UHI::NativeMenuLifecycle g_menuLifecycle;
    std::atomic_bool g_cursorOwnedByHost{};
    std::atomic_uint64_t g_pendingCursorShow{};
    std::atomic_bool g_resetImGuiInput{ true };
    std::atomic_int g_pendingMouseWheelSteps{};
    std::mutex g_rendererMutex;
    ImGuiContext* g_imguiContext{};
    bool g_win32Initialized{};
    bool g_dx11Initialized{};
    bool g_rendererInitialized{};
    std::string g_imguiIniPath;
    HWND g_outputWindow{};
    WNDPROC g_previousWindowProc{};
    std::atomic_bool g_readableTheme{};
    ImFont* g_standardFont{};
    std::mutex g_modalMouseMutex;
    std::vector<std::pair<unsigned, bool>> g_modalMouseEvents;

    LRESULT CALLBACK UhmWindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
    {
        // Skyrim's Scaleform route does not consistently forward wheel events
        // to a native IMenu.  Receive the actual Windows message as the
        // authoritative fallback, so ImGui tables always scroll under the
        // pointer.  We consume it only while UHM owns the open menu.
        if (message == WM_MOUSEWHEEL && g_menuLifecycle.CanRender() && g_rendererInitialized && g_imguiContext) {
            const auto delta = static_cast<float>(GET_WHEEL_DELTA_WPARAM(wParam)) /
                static_cast<float>(WHEEL_DELTA);
            if (delta != 0.0F) {
                UHI::NativeImGuiHost::SubmitMouseWheel(delta);
                return 0;
            }
        }
        return g_previousWindowProc ?
            CallWindowProc(g_previousWindowProc, window, message, wParam, lParam) :
            DefWindowProc(window, message, wParam, lParam);
    }

    bool InstallWindowMessageHook(HWND window)
    {
        if (!window || g_previousWindowProc) return g_previousWindowProc != nullptr;
        SetLastError(0);
        const auto previous = SetWindowLongPtr(window, GWLP_WNDPROC,
            reinterpret_cast<LONG_PTR>(&UhmWindowProc));
        if (previous == 0 && GetLastError() != 0) {
            SKSE::log::warn("UHM could not install the native mouse-wheel message hook");
            return false;
        }
        g_previousWindowProc = reinterpret_cast<WNDPROC>(previous);
        return true;
    }

    void RemoveWindowMessageHook()
    {
        if (!g_outputWindow || !g_previousWindowProc) return;
        if (reinterpret_cast<WNDPROC>(GetWindowLongPtr(g_outputWindow, GWLP_WNDPROC)) == &UhmWindowProc) {
            SetWindowLongPtr(g_outputWindow, GWLP_WNDPROC,
                reinterpret_cast<LONG_PTR>(g_previousWindowProc));
            g_previousWindowProc = nullptr;
        }
        // A later plugin may still chain through UhmWindowProc. Keep its
        // predecessor callable even when our renderer initialization fails.
    }

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

    bool BuildFontAtlas(ImGuiIO& io)
    {
        const auto fonts = UHI::BuildNativeFontAtlas(*io.Fonts);
        io.FontDefault = g_standardFont = fonts.standard;
        const auto preferences = UHI::LoadOpeningHotkey(std::filesystem::current_path() /
            "Data/SKSE/Plugins/UniversalHotkeyManager.ini");
        g_readableTheme = preferences.readableTheme;
        if (!fonts.valid) {
            SKSE::log::error("UHM font atlas is invalid for D3D11: {}x{}", io.Fonts->TexWidth, io.Fonts->TexHeight);
            return false;
        }
        SKSE::log::info("Built UHM Windows English/Korean/Chinese font atlas: {}x{}, system UI font={}",
            io.Fonts->TexWidth, io.Fonts->TexHeight, fonts.systemDefaultUsed);
        return true;
    }
    bool ValidateFontTexture(ID3D11Device* device, ImGuiIO& io)
    {
        if (!device || !io.Fonts || !io.Fonts->IsBuilt()) return false;

        unsigned char* pixels{};
        int width{};
        int height{};
        io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
        if (!pixels || width <= 0 || height <= 0) {
            SKSE::log::error("Could not obtain the UHM font-atlas pixels");
            return false;
        }

        D3D11_TEXTURE2D_DESC description{};
        description.Width = static_cast<UINT>(width);
        description.Height = static_cast<UINT>(height);
        description.MipLevels = 1;
        description.ArraySize = 1;
        description.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        description.SampleDesc.Count = 1;
        description.Usage = D3D11_USAGE_DEFAULT;
        description.BindFlags = D3D11_BIND_SHADER_RESOURCE;

        D3D11_SUBRESOURCE_DATA data{};
        data.pSysMem = pixels;
        data.SysMemPitch = description.Width * 4U;

        ID3D11Texture2D* texture{};
        const auto result = device->CreateTexture2D(&description, &data, &texture);
        if (texture) texture->Release();
        if (FAILED(result)) {
            SKSE::log::error(
                "D3D11 rejected the UHM font texture {}x{} (HRESULT 0x{:08X}); native UI remains disabled",
                width, height, static_cast<std::uint32_t>(result));
            return false;
        }
        return true;
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
        // Editor and notification popups must remain fully opaque even when
        // the user lowers the transparency of the main manager window.
        colors[ImGuiCol_PopupBg] = HexColor(0x000000FFU);
        // Keep controls distinct from the window without making them look
        // boxed-in.  A near-black blue-grey fill and a low-contrast border
        // remain readable over both the game world and opaque popups.
        colors[ImGuiCol_Border] = HexColor(0x52606F73U);
        colors[ImGuiCol_BorderShadow] = HexColor(0x00000000U);
        colors[ImGuiCol_FrameBg] = HexColor(0x080B10EBU);
        colors[ImGuiCol_FrameBgHovered] = HexColor(0x101722F2U);
        colors[ImGuiCol_FrameBgActive] = HexColor(0x172536FAU);
        colors[ImGuiCol_TitleBg] = HexColor(0x000000FFU);
        colors[ImGuiCol_TitleBgActive] = HexColor(0x000000FFU);
        colors[ImGuiCol_TitleBgCollapsed] = HexColor(0x000000FFU);
        colors[ImGuiCol_MenuBarBg] = HexColor(0x00000000U);
        colors[ImGuiCol_ScrollbarBg] = HexColor(0xFFFFFF33U);
        colors[ImGuiCol_ScrollbarGrab] = HexColor(0xFFFFFF33U);
        colors[ImGuiCol_ScrollbarGrabHovered] = HexColor(0xFFFFFF33U);
        colors[ImGuiCol_ScrollbarGrabActive] = HexColor(0xFFFFFF66U);
        colors[ImGuiCol_CheckMark] = HexColor(0xFFFFFFFFU);
        colors[ImGuiCol_SliderGrab] = HexColor(0x526779C2U);
        colors[ImGuiCol_SliderGrabActive] = HexColor(0x6D8CA8E6U);
        colors[ImGuiCol_Button] = HexColor(0x222426F0U);
        colors[ImGuiCol_ButtonHovered] = HexColor(0x303338F7U);
        colors[ImGuiCol_ButtonActive] = HexColor(0x404247FFU);
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

    class ScopedImGuiContext final
    {
    public:
        explicit ScopedImGuiContext(ImGuiContext* context) noexcept :
            previous_(ImGui::GetCurrentContext())
        {
            ImGui::SetCurrentContext(context);
        }

        ~ScopedImGuiContext()
        {
            ImGui::SetCurrentContext(previous_);
        }

        ScopedImGuiContext(const ScopedImGuiContext&) = delete;
        ScopedImGuiContext& operator=(const ScopedImGuiContext&) = delete;

    private:
        ImGuiContext* previous_{};
    };

    void ResetRendererInitialization()
    {
        RemoveWindowMessageHook();
        auto* previous = ImGui::GetCurrentContext();
        if (g_imguiContext) ImGui::SetCurrentContext(g_imguiContext);

        if (g_dx11Initialized) {
            ImGui_ImplDX11_Shutdown();
            g_dx11Initialized = false;
        }
        if (g_win32Initialized) {
            ImGui_ImplWin32_Shutdown();
            g_win32Initialized = false;
        }
        if (g_imguiContext) {
            auto* destroyed = g_imguiContext;
            ImGui::DestroyContext(g_imguiContext);
            g_imguiContext = nullptr;
            if (previous == destroyed) previous = nullptr;
        }

        ImGui::SetCurrentContext(previous);
        g_rendererInitialized = false;
        g_outputWindow = nullptr;
    }

    bool InitializeRenderer(IDXGISwapChain* swapChain, ID3D11Device* device,
        ID3D11DeviceContext* context)
    {
        std::scoped_lock lock(g_rendererMutex);
        if (g_rendererInitialized) return true;
        if (!swapChain || !device || !context) {
            SKSE::log::error("Could not initialize UHM ImGui: incomplete Skyrim DirectX 11 state");
            return false;
        }
        DXGI_SWAP_CHAIN_DESC description{};
        if (FAILED(swapChain->GetDesc(&description)) || !description.OutputWindow) {
            SKSE::log::error("Could not initialize UHM ImGui: swap-chain window is unavailable");
            return false;
        }

        IMGUI_CHECKVERSION();
        auto* previousContext = ImGui::GetCurrentContext();
        g_imguiContext = ImGui::CreateContext();
        if (!g_imguiContext) {
            SKSE::log::error("Could not initialize UHM ImGui: context creation failed");
            ImGui::SetCurrentContext(previousContext);
            return false;
        }
        // ImGui::CreateContext restores an already-active foreign context.
        // UHM owns a separate context, so every setup call must explicitly
        // target it before the Win32/DX11 backends attach their user data.
        ImGui::SetCurrentContext(g_imguiContext);
        ApplyLegacyStyle();
        auto& io = ImGui::GetIO();
        g_imguiIniPath = (std::filesystem::current_path() /
            "Data/SKSE/Plugins/UniversalHotkeyManager/imgui.ini").string();
        io.IniFilename = g_imguiIniPath.c_str();
        io.LogFilename = nullptr;
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_NavEnableGamepad;
        io.ConfigWindowsMoveFromTitleBarOnly = true;
        if (!BuildFontAtlas(io)) {
            ResetRendererInitialization();
            ImGui::SetCurrentContext(previousContext);
            return false;
        }
        // The stock ImGui DX11 backend assumes CreateTexture2D succeeds and
        // dereferences the returned pointer in release builds. Validate the
        // exact atlas descriptor and pixels before attaching the backend so a
        // driver rejection fails closed instead of crashing on the first menu
        // frame.
        if (!ValidateFontTexture(device, io)) {
            ResetRendererInitialization();
            ImGui::SetCurrentContext(previousContext);
            return false;
        }
        g_outputWindow = description.OutputWindow;

        g_win32Initialized = ImGui_ImplWin32_Init(description.OutputWindow);
        g_dx11Initialized = g_win32Initialized && ImGui_ImplDX11_Init(device, context);

        // ImGui_ImplDX11_Init owns device-object creation. Calling
        // ImGui_ImplDX11_CreateDeviceObjects here before Skyrim has completed its
        // D3D initialization caused the first hotkey press to create an invisible
        // pausing menu and the second press to dereference a partial backend.
        if (!g_win32Initialized || !g_dx11Initialized ||
            !io.BackendPlatformUserData || !io.BackendRendererUserData) {
            SKSE::log::error("Could not initialize UHM ImGui Win32/DX11 backends");
            ResetRendererInitialization();
            ImGui::SetCurrentContext(previousContext);
            return false;
        }
        g_rendererInitialized = true;
        if (!InstallWindowMessageHook(description.OutputWindow)) {
            SKSE::log::warn("UHM will use the Skyrim input fallback for mouse-wheel scrolling");
        }
        ImGui::SetCurrentContext(previousContext);
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
        if (!g_menuLifecycle.CanRender() || !data || !data->scaleformEvent || !g_rendererInitialized || !g_imguiContext) return;
        const ScopedImGuiContext context(g_imguiContext);
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
        ~NativeMenu() override
        {
            if (g_menuLifecycle.IsOpen()) {
                g_menuLifecycle.Closed();
                FinishClose();
            }
        }

        void PostDisplay() override
        {
            if (!g_menuLifecycle.CanRender() || !g_rendererInitialized || !g_win32Initialized || !g_dx11Initialized || !g_imguiContext) return;
            const ScopedImGuiContext context(g_imguiContext);
            const bool resetInput = g_resetImGuiInput.exchange(false);
            if (resetInput) {
                auto& io = ImGui::GetIO();
                io.ClearEventsQueue();
                io.ClearInputKeys();
                io.ClearInputMouse();
            }
            ImGui_ImplWin32_NewFrame();
            ImGui_ImplDX11_NewFrame();
            // Scaleform sends aggregate modifiers. Publish physical left/right
            // keys as well, so capture still works when another sink stops the
            // Skyrim input batch before it reaches this menu.
            auto& io = ImGui::GetIO();
            const bool focused = GetForegroundWindow() == g_outputWindow;
            for (const auto& [key, vk] : std::array{
                std::pair{ImGuiKey_LeftCtrl, VK_LCONTROL}, std::pair{ImGuiKey_RightCtrl, VK_RCONTROL},
                std::pair{ImGuiKey_LeftShift, VK_LSHIFT}, std::pair{ImGuiKey_RightShift, VK_RSHIFT},
                std::pair{ImGuiKey_LeftAlt, VK_LMENU}, std::pair{ImGuiKey_RightAlt, VK_RMENU}})
                io.AddKeyEvent(key, focused && (GetAsyncKeyState(vk) & 0x8000) != 0);
            io.FontDefault = g_standardFont;
            ImGui::GetStyle().Colors[ImGuiCol_Text] = g_readableTheme.load() ?
                ImVec4(1.0F, 0.95F, 0.82F, 1.0F) : ImVec4(0.94F, 0.96F, 0.99F, 1.0F);
            UpdateMousePosition();
            {
                std::scoped_lock lock(g_modalMouseMutex);
                for (const auto& [button, down] : g_modalMouseEvents)
                    io.AddMouseButtonEvent(static_cast<int>(button), down);
                g_modalMouseEvents.clear();
            }
            const auto wheelSteps = g_pendingMouseWheelSteps.exchange(0, std::memory_order_relaxed);
            if (wheelSteps != 0) {
                ImGui::GetIO().AddMouseWheelEvent(0.0F, static_cast<float>(wheelSteps));
            }
            ImGui::NewFrame();
            if (resetInput) UHI::ResetNativeMenuPopups();
            if (g_renderCallback) g_renderCallback();
            ImGui::Render();
            ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
            ForceCursor();
        }

        RE::UI_MESSAGE_RESULTS ProcessMessage(RE::UIMessage& message) override
        {
            const auto change = g_menuLifecycle.ProcessMessage(message.type.get());
            if (change == UHI::NativeMenuLifecycle::Change::hidden) {
                FinishClose();
            } else if (change == UHI::NativeMenuLifecycle::Change::shown) {
                g_resetImGuiInput = true;
                // A show message already posted to Skyrim can arrive after
                // its request was cancelled. Do not leave an invisible menu
                // on the engine stack holding the pause/input context.
                if (!g_menuLifecycle.CanRender()) {
                    if (auto* queue = RE::UIMessageQueue::GetSingleton())
                        queue->AddMessage(kMenuName, RE::UI_MESSAGE_TYPE::kHide, nullptr);
                }
            } else if (message.type == RE::UI_MESSAGE_TYPE::kScaleformEvent) {
                ProcessScaleformEvent(reinterpret_cast<const RE::BSUIScaleformData*>(message.data));
                return RE::UI_MESSAGE_RESULTS::kHandled;
            }
            return IMenu::ProcessMessage(message);
        }

        static void FinishClose()
        {
            g_pendingCursorShow = 0;
            g_pendingMouseWheelSteps = 0;
            {
                std::scoped_lock lock(g_modalMouseMutex);
                g_modalMouseEvents.clear();
            }
            g_resetImGuiInput = true;
            if (g_closeCallback) g_closeCallback();
            ReleaseCursor();
        }

        static RE::IMenu* Creator()
        {
            using Flags = RE::UI_MENU_FLAGS;
            auto* menu = new NativeMenu();
            // Match the proven native ImGui IMenu lifecycle used by SFS. Extra
            // modal/topmost flags are intentionally omitted because they alter
            // Skyrim's cursor and pause-menu ownership during transitions.
            menu->menuFlags.set(Flags::kUpdateUsesCursor, Flags::kUsesCursor,
                Flags::kCustomRendering, Flags::kUsesMenuContext, Flags::kPausesGame);
            menu->depthPriority = 11;
            menu->inputContext.set(Context::kMenuMode);
            return menu;
        }

    private:
        static void ReleaseCursor()
        {
            if (!g_cursorOwnedByHost.load()) return;
            const auto ticket = g_menuLifecycle.Revision();
            if (auto* tasks = SKSE::GetTaskInterface()) {
                tasks->AddUITask([ticket] {
                    if (!g_menuLifecycle.IsCurrent(ticket) || g_menuLifecycle.CanRender()) return;
                    // A different menu may have acquired the cursor since UHM
                    // closed. Its normal engine lifecycle must retain control.
                    if (auto* ui = RE::UI::GetSingleton()) {
                        const auto cursor = ui->GetMenu(RE::CursorMenu::MENU_NAME);
                        const auto ownMenu = ui->GetMenu(kMenuName);
                        for (const auto& menu : ui->menuStack)
                            if (menu && menu.get() != cursor.get() && menu.get() != ownMenu.get() && menu->UsesCursor()) {
                                g_cursorOwnedByHost = false;
                                return;
                            }
                    }
                    if (auto* queue = RE::UIMessageQueue::GetSingleton()) {
                        g_cursorOwnedByHost = false;
                        queue->AddMessage(RE::CursorMenu::MENU_NAME, RE::UI_MESSAGE_TYPE::kHide, nullptr);
                    }
                });
            }
        }

        static void ForceCursor()
        {
            if (!g_menuLifecycle.CanRender()) return;
            auto* ui = RE::UI::GetSingleton();
            if (!ui || ui->IsMenuOpen(RE::CursorMenu::MENU_NAME)) return;
            const auto ticket = g_menuLifecycle.Revision();
            std::uint64_t expected{};
            if (!g_pendingCursorShow.compare_exchange_strong(expected, ticket)) return;
            if (auto* tasks = SKSE::GetTaskInterface()) {
                tasks->AddUITask([ticket] {
                    auto pending = ticket;
                    if (!g_pendingCursorShow.compare_exchange_strong(pending, 0)) return;
                    if (!g_menuLifecycle.IsCurrent(ticket) || !g_menuLifecycle.CanRender()) return;
                    auto* currentUi = RE::UI::GetSingleton();
                    if (!currentUi || currentUi->IsMenuOpen(RE::CursorMenu::MENU_NAME)) return;
                    if (auto* queue = RE::UIMessageQueue::GetSingleton()) {
                        g_cursorOwnedByHost = true;
                        queue->AddMessage(RE::CursorMenu::MENU_NAME, RE::UI_MESSAGE_TYPE::kShow, nullptr);
                    }
                });
            } else {
                g_pendingCursorShow = 0;
            }
        }
    };

    bool RegisterNativeMenu()
    {
        if (g_registered.load()) return true;
        if (auto* ui = RE::UI::GetSingleton()) {
            ui->Register(kMenuName, NativeMenu::Creator);
            g_registered = true;
            SKSE::log::info("Registered UHM native IMenu host after D3D initialization");
            return true;
        }
        SKSE::log::error("Could not register UHM native IMenu host: UI singleton unavailable");
        return false;
    }

    struct D3DInitHook
    {
        static void Thunk()
        {
            func();
            auto* renderer = RE::BSGraphics::Renderer::GetSingleton();
            if (!renderer) {
                SKSE::log::critical("UHM renderer hook ran without Skyrim renderer");
                return;
            }
            const auto& runtime = renderer->GetRuntimeData();
            if (!runtime.renderWindows || !runtime.renderWindows->swapChain) {
                SKSE::log::critical("UHM renderer hook ran without Skyrim swap chain");
                return;
            }
            auto* swapChain = reinterpret_cast<IDXGISwapChain*>(runtime.renderWindows->swapChain);
            auto* device = reinterpret_cast<ID3D11Device*>(runtime.forwarder);
            auto* context = reinterpret_cast<ID3D11DeviceContext*>(runtime.context);
            if (InitializeRenderer(swapChain, device, context)) RegisterNativeMenu();
        }

        static inline REL::Relocation<decltype(Thunk)> func;
    };

    bool QueueVisibility(const bool open)
    {
        auto* tasks = SKSE::GetTaskInterface();
        if (!tasks) return false;
        const bool wasOpen = g_menuLifecycle.IsOpen();
        const auto ticket = g_menuLifecycle.Request(open);
        try {
            tasks->AddUITask([open, ticket] {
                if (!g_menuLifecycle.IsCurrent(ticket)) return;
                if (auto* queue = RE::UIMessageQueue::GetSingleton()) {
                    queue->AddMessage(kMenuName, open ? RE::UI_MESSAGE_TYPE::kShow : RE::UI_MESSAGE_TYPE::kHide, nullptr);
                } else {
                    g_menuLifecycle.Closed();
                    NativeMenu::FinishClose();
                }
            });
        } catch (...) {
            if (g_menuLifecycle.IsCurrent(ticket)) g_menuLifecycle.Request(wasOpen);
            SKSE::log::error("Unable to queue UHM menu visibility request");
            return false;
        }
        return true;
    }
}

namespace UHI::NativeImGuiHost
{
    void SubmitModalMouseButton(const unsigned button, const bool down) noexcept
    {
        if (!g_menuLifecycle.CanRender() || button >= 5U) return;
        try {
            std::scoped_lock lock(g_modalMouseMutex);
            if (g_modalMouseEvents.size() < 128U) g_modalMouseEvents.emplace_back(button, down);
        } catch (...) {}
    }
    void SetAppearance(const bool readableTheme) noexcept
    {
        g_readableTheme = readableTheme;
    }
    void SubmitMouseWheel(const float delta) noexcept
    {
        if (!g_menuLifecycle.CanRender()) return;
        if (delta > 0.0F) {
            g_pendingMouseWheelSteps.fetch_add(1, std::memory_order_relaxed);
        } else if (delta < 0.0F) {
            g_pendingMouseWheelSteps.fetch_sub(1, std::memory_order_relaxed);
        }
    }

    bool InstallRendererHook()
    {
        if (g_rendererHookInstalled.load()) return true;
        const auto version = REL::Module::get().version();
        const auto layout = NativeRendererHookLayoutForVersion(
            version.major(), version.minor(), version.patch(), version.build());
        if (!layout) {
            SKSE::log::critical("UHM native renderer disabled: Skyrim {} has no verified D3D hook layout",
                version.string());
            return false;
        }

        const auto callSite = REL::ID(layout->d3dInitRelocationID).address() +
            layout->d3dInitCallOffset;
        if (*reinterpret_cast<const std::uint8_t*>(callSite) != 0xE8) {
            SKSE::log::critical("UHM native renderer disabled: unexpected opcode at D3D init hook site");
            return false;
        }
        auto& trampoline = SKSE::GetTrampoline();
        D3DInitHook::func = trampoline.write_call<5>(callSite, D3DInitHook::Thunk);
        g_rendererHookInstalled = true;
        SKSE::log::info("Installed UHM D3D initialization hook for Skyrim {}", version.string());
        return true;
    }

    bool Register(const RenderCallback callback, const CloseCallback closed)
    {
        if (!callback) return false;
        g_renderCallback = callback;
        g_closeCallback = closed;
        return true;
    }

    bool Open()
    {
        if (!IsReady()) return false;
        if (!g_menuLifecycle.WantsOpen()) return QueueVisibility(true);
        return true;
    }

    bool Toggle()
    {
        if (!IsReady()) return false;
        return QueueVisibility(!g_menuLifecycle.IsOpen());
    }

    bool Close()
    {
        if (!g_registered.load() || !g_menuLifecycle.IsOpen()) return false;
        return QueueVisibility(false);
    }

    bool IsOpen() noexcept
    {
        return g_menuLifecycle.IsOpen();
    }

    bool IsReady() noexcept
    {
        return g_registered.load() && g_rendererInitialized && g_imguiContext &&
            g_win32Initialized && g_dx11Initialized;
    }
}
