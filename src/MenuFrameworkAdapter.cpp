#include "UHI/MenuFrameworkAdapter.h"
#include "UHI/BindingSerializer.h"
#include "UHI/Registry.h"
#include "UHI/HotkeyViewModel.h"
#include "UHI/writers/ControlMapWriter.h"
#include "UHI/writers/ConfigFileWriter.h"
#include "UHI/OpeningHotkey.h"
#include "UHI/InputCodeFormatter.h"
#include "UHI/PathEncoding.h"
#include <SKSE/Logger.h>
#ifdef UHI_ENABLE_MENU_FRAMEWORK
#include <SKSEMenuFramework.h>
#include <RE/R/Renderer.h>
#include <d3d11.h>
#include <wincodec.h>
#include <wrl/client.h>
#ifdef interface
#undef interface
#endif
#include <mutex>
#include <atomic>
#include <string>
#include <array>
#include <algorithm>
#include <climits>
#include <cstdio>
#include <charconv>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <memory>
#include <optional>
#include <sstream>
#include <span>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace
{
    std::atomic<UHI::UiLanguage> g_uiLanguage{ UHI::UiLanguage::automatic };

    UHI::UiLanguage EffectiveUiLanguage() noexcept
    {
        return UHI::ResolveUiLanguage(g_uiLanguage.load());
    }

    const char* UiText(const char* english, const char* korean, const char* chinese) noexcept
    {
        switch (EffectiveUiLanguage()) {
        case UHI::UiLanguage::korean: return korean;
        case UHI::UiLanguage::chinese: return chinese;
        default: return english;
        }
    }

    const char* ContextConfidenceName(UHI::ContextConfidence confidence);
    std::string LocalizedContextLabel(std::uint32_t mask, bool compact = false);

    enum class HotkeyViewPage
    {
        overview,
        category,
        options
    };

    SKSEMenuFramework::Model::WindowInterface* g_window = nullptr;
    struct RegistrySnapshot
    {
        std::shared_ptr<const UHI::Registry> registry;
        std::vector<UHI::HotkeyViewGroup> groups;
        std::size_t conflictCount{};
        std::size_t conditionalConflictCount{};
        bool restored{};
    };

    std::shared_ptr<const RegistrySnapshot> g_registrySnapshot;
    std::mutex g_registryMutex;
    // These are render-thread-only. Filtering and sorting can be expensive for
    // large mod lists, so recompute only when a filter or immutable scan
    // snapshot changes instead of on every game frame.
    std::shared_ptr<const RegistrySnapshot> g_filteredSnapshot;
    std::vector<UHI::HotkeyViewGroup> g_filteredGroups;
    UHI::HotkeyCategory g_filteredCategory = UHI::HotkeyCategory::all;
    int g_filteredDevice = -1;
    bool g_filteredConflictsOnly = false;
    bool g_filteredOverlapsOnly = false;
    bool g_filteredConflictsFirst = false;
    std::string g_filteredSearch;
    UHI::HotkeyCategory g_category = UHI::HotkeyCategory::all;
    HotkeyViewPage g_page = HotkeyViewPage::overview;
    bool g_conflictsOnly = false;
    bool g_overlapsOnly = false;
    int g_device = 0;
    char g_search[128]{};
    bool g_conflictsFirst = true;
    bool g_categoryMapView = true;
    std::mutex g_statusMutex;
    bool g_scanRunning = false;
    float g_scanPercent = 0.0F;
    float g_scanFilePercent = 0.0F;
    std::string g_scanStage;
    std::string g_scanPath;
    std::function<void()> g_startScan;
    std::function<void()> g_automaticRefresh;
    std::function<void()> g_cancelScan;
    std::function<UHI::OpeningHotkey()> g_getOpeningHotkey;
    std::function<bool(const UHI::OpeningHotkey&)> g_saveOpeningHotkey;
    UHI::BindingWriter g_bindingWriter;
    UHI::OpeningHotkey g_openingHotkeyDraft;
    bool g_openingHotkeyDraftLoaded = false;
    bool g_preferencesDirty = false;
    std::mutex g_preferencesMutex;
    std::atomic_bool g_openingHotkeyCaptureActive{ false };
    float g_uiScale = 1.0F;
    float g_windowOpacity = 0.90F;
    enum class EditorModal
    {
        none,
        rename,
        binding
    };

    EditorModal g_editorModal{ EditorModal::none };
    std::atomic_bool g_modalInputActive{ false };
    std::atomic_bool g_cancelActivePopupRequested{ false };
    std::atomic_bool g_renderWindowVisible{ false };
    // Set whenever UHM is opened outside an already-focused Menu Framework
    // entry.  The next render frame explicitly gives the ImGui window focus.
    std::atomic_bool g_focusWindowOnNextRender{ false };
    std::atomic_bool g_suppressWindowCloseUntilEscapeRelease{ false };
    std::atomic_int64_t g_suppressWindowCloseUntilMilliseconds{ 0 };
    bool g_escapeConsumedByPopupThisFrame{};
    std::atomic_bool g_bindingCaptureActive{ false };
    std::mutex g_editorMutex;
    std::optional<UHI::HotkeyRecord> g_editorRecord;
    char g_renameActionValue[513]{};
    bool g_focusRenameField{};
    std::string g_bindingCaptureDisplay;
    std::string g_bindingCaptureRaw;
    std::string g_bindingCaptureStatus;
    std::atomic_bool g_bindingWritePending{ false };
    std::atomic_int g_bindingWriteResult{ 0 };  // 0 waiting/none, 1 success, 2 failure
    std::mutex g_bindingWriteResultMutex;
    std::string g_bindingWriteResultMessage;
    int g_bindingCaptureArmedFrame{ -1 };
    int g_openingHotkeyCaptureArmedFrame{ -1 };
    int g_editorPopupOpenNotBeforeFrame{ -1 };
    bool g_openRenamePopup{};
    bool g_openBindingPopup{};
    std::atomic_size_t g_pendingChangedHotkeyNotice{};
    std::size_t g_visibleChangedHotkeyNotice{};
    enum class BindingWriteNotice : int
    {
        none,
        gameSaveRequired,
        documentRolledBack,
        documentRollbackUnverified,
        fontRestartRequired
    };
    std::atomic_int g_pendingBindingWriteNotice{};
    BindingWriteNotice g_visibleBindingWriteNotice{ BindingWriteNotice::none };
    bool g_automaticRefreshRequestedForCurrentOpen{};
    bool g_openBindingContextPopup{};
    bool g_bindingContextPopupActive{};
    int g_bindingHotspotBlockedUntilFrame{ -1 };
    std::optional<UHI::HotkeyRecord> g_contextRecord;
    std::unordered_map<std::string, std::string> g_actionNameOverrides;
    bool g_actionNameOverridesLoaded{};
    bool g_missingImGuiContextLogged = false;
    std::atomic_bool g_sexLabInstalled{ false };

    std::int64_t MonotonicMilliseconds() noexcept
    {
        return std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
    }

    void SuppressWindowCloseForEscapePress() noexcept
    {
        // The same physical Escape press may be observed once by ImGui and
        // once by Skyrim's input sink.  Keep it from closing both the popup
        // and the parent window, but never leave a permanent latch behind if
        // a particular Menu Framework/input configuration omits the key-up
        // event.
        g_suppressWindowCloseUntilEscapeRelease = true;
        g_suppressWindowCloseUntilMilliseconds.store(MonotonicMilliseconds() + 500);
    }

    bool HasMenuFrameworkContext()
    {
        if (ImGui::GetCurrentContext()) {
            return true;
        }
        if (!g_missingImGuiContextLogged) {
            SKSE::log::error("Menu Framework invoked UHI without an ImGui context; rendering was skipped");
            g_missingImGuiContextLogged = true;
        }
        if (g_window) {
            g_window->IsOpen = false;
        }
        return false;
    }

    bool SavePendingPreferences()
    {
        std::function<bool(const UHI::OpeningHotkey&)> save;
        {
            std::scoped_lock lock(g_statusMutex);
            save = g_saveOpeningHotkey;
        }

        UHI::OpeningHotkey draft;
        {
            std::scoped_lock lock(g_preferencesMutex);
            if (!g_openingHotkeyDraftLoaded || !g_preferencesDirty) return true;
            draft = g_openingHotkeyDraft;
        }
        if (!save || !UHI::IsValidOpeningHotkey(draft)) {
            SKSE::log::error("Could not auto-save UHM preferences: invalid shortcut or unavailable writer");
            return false;
        }

        const bool saved = save(draft);
        if (saved) {
            std::scoped_lock lock(g_preferencesMutex);
            if (g_openingHotkeyDraft == draft) g_preferencesDirty = false;
            SKSE::log::info("UHM shortcut, text size, window opacity, and UI language saved automatically while closing the window");
        } else {
            SKSE::log::error("Could not auto-save UHM preferences to UniversalHotkeyManager.ini");
        }
        return saved;
    }

    std::filesystem::path ActionOverridePath()
    {
        return std::filesystem::current_path() / L"Data" / L"SKSE" / L"Plugins" /
            L"UniversalHotkeyManager.ActionNames.ini";
    }

    std::string HexEncode(const std::string_view value)
    {
        constexpr char digits[] = "0123456789ABCDEF";
        std::string output;
        output.reserve(value.size() * 2U);
        for (const auto ch : value) {
            const auto byte = static_cast<unsigned char>(ch);
            output.push_back(digits[byte >> 4U]);
            output.push_back(digits[byte & 0x0FU]);
        }
        return output;
    }

    std::optional<std::string> HexDecode(const std::string_view value)
    {
        if ((value.size() & 1U) != 0U) return std::nullopt;
        const auto digit = [](const char ch) -> int {
            if (ch >= '0' && ch <= '9') return ch - '0';
            if (ch >= 'A' && ch <= 'F') return ch - 'A' + 10;
            if (ch >= 'a' && ch <= 'f') return ch - 'a' + 10;
            return -1;
        };
        std::string output;
        output.reserve(value.size() / 2U);
        for (std::size_t index = 0; index < value.size(); index += 2U) {
            const int high = digit(value[index]);
            const int low = digit(value[index + 1U]);
            if (high < 0 || low < 0) return std::nullopt;
            output.push_back(static_cast<char>((high << 4) | low));
        }
        return output;
    }

    std::string ActionIdentity(const UHI::HotkeyRecord& record)
    {
        return UHI::PathToUtf8(record.evidencePath) + '\x1F' +
            std::to_string(record.evidenceLine) + '\x1F' + record.detector + '\x1F' +
            record.settingSection + '\x1F' + record.settingName + '\x1F' + record.rawBinding;
    }

    void EnsureActionOverridesLoaded()
    {
        if (g_actionNameOverridesLoaded) return;
        g_actionNameOverridesLoaded = true;
        std::ifstream input(ActionOverridePath(), std::ios::binary);
        if (!input) return;
        for (std::string line; std::getline(input, line);) {
            if (line.empty() || line.front() == '[' || line.front() == ';' || line.front() == '#') continue;
            const auto equals = line.find('=');
            if (equals == std::string::npos) continue;
            const auto key = HexDecode(std::string_view(line).substr(0, equals));
            const auto value = HexDecode(std::string_view(line).substr(equals + 1U));
            if (key && value && !key->empty() && !value->empty()) {
                g_actionNameOverrides.insert_or_assign(*key, *value);
            }
        }
    }

    bool SaveActionOverrides()
    {
        const auto path = ActionOverridePath();
        std::error_code error;
        std::filesystem::create_directories(path.parent_path(), error);
        if (error) return false;
        auto temporary = path;
        temporary += L".tmp";
        std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
        if (!output) return false;
        output << "[ActionNames]\n";
        for (const auto& [key, value] : g_actionNameOverrides) {
            output << HexEncode(key) << '=' << HexEncode(value) << '\n';
        }
        output.flush();
        if (!output) return false;
        output.close();
#ifdef _WIN32
        if (!MoveFileExW(temporary.c_str(), path.c_str(),
                MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
            std::filesystem::remove(temporary, error);
            return false;
        }
#else
        std::filesystem::rename(temporary, path, error);
        if (error) return false;
#endif
        return true;
    }

    std::string DisplayAction(const UHI::HotkeyRecord& record)
    {
        EnsureActionOverridesLoaded();
        if (const auto found = g_actionNameOverrides.find(ActionIdentity(record));
            found != g_actionNameOverrides.end()) return found->second;
        return record.action;
    }

    void CloseEditorModal()
    {
        std::scoped_lock lock(g_editorMutex);
        g_editorModal = EditorModal::none;
        g_editorRecord.reset();
        g_bindingCaptureActive = false;
        g_modalInputActive = false;
        g_bindingCaptureDisplay.clear();
        g_bindingCaptureRaw.clear();
        g_bindingCaptureStatus.clear();
        g_bindingWritePending = false;
        g_bindingWriteResult = 0;
        {
            std::scoped_lock resultLock(g_bindingWriteResultMutex);
            g_bindingWriteResultMessage.clear();
        }
        g_openRenamePopup = false;
        g_openBindingPopup = false;
        g_focusRenameField = false;
        g_cancelActivePopupRequested = false;
    }

    bool ConsumePopupCancelRequest()
    {
        const bool requested = g_cancelActivePopupRequested.exchange(false);
        const bool escapePressed = ImGui::IsKeyPressed(ImGuiKey_Escape, false);
        if (requested || escapePressed) {
            // The render fallback runs after every popup. Record that this
            // physical Escape has already cancelled one UI layer so it can
            // never close the parent window during the same frame.
            g_escapeConsumedByPopupThisFrame = true;
            SuppressWindowCloseForEscapePress();
        }
        return requested || escapePressed;
    }

    void BeginActionRename(const UHI::HotkeyRecord& record)
    {
        std::scoped_lock lock(g_editorMutex);
        g_editorRecord = record;
        const auto current = DisplayAction(record);
        std::snprintf(g_renameActionValue, sizeof(g_renameActionValue), "%s", current.c_str());
        g_editorModal = EditorModal::rename;
        g_modalInputActive = true;
        g_bindingCaptureActive = false;
        g_focusRenameField = true;
        g_openBindingPopup = false;
        g_openRenamePopup = true;
        // RenderEditorPopups runs after every device/list child and after the
        // context popup has ended, so the modal can be requested immediately
        // from the stable root-window ID stack.  Deferring to the next frame
        // loses OpenPopup requests in some SKSE Menu Framework builds.
        g_editorPopupOpenNotBeforeFrame = ImGui::GetFrameCount();
        SKSE::log::info("Rename-action popup queued for '{}' ({})", record.owner, record.binding);
    }

    std::string FriendlyBindingLabel(std::string_view binding);

    void BeginBindingEdit(const UHI::HotkeyRecord& record)
    {
        if (!record.editable || record.detector == "BuiltInHotkeyTable") return;
        std::scoped_lock lock(g_editorMutex);
        g_editorRecord = record;
        g_editorModal = EditorModal::binding;
        g_modalInputActive = true;
        g_bindingCaptureActive = false;
        g_bindingCaptureDisplay = FriendlyBindingLabel(record.binding);
        g_bindingCaptureRaw.clear();
        g_bindingCaptureStatus.clear();
        g_bindingWritePending = false;
        g_bindingWriteResult = 0;
        {
            std::scoped_lock resultLock(g_bindingWriteResultMutex);
            g_bindingWriteResultMessage.clear();
        }
        g_focusRenameField = false;
        g_openRenamePopup = false;
        g_openBindingPopup = true;
        // RenderEditorPopups is already outside the originating child/context
        // popup here.  Open from that stable root stack in this frame and keep
        // retrying until BeginPopupModal confirms that it is visible.
        g_editorPopupOpenNotBeforeFrame = ImGui::GetFrameCount();
        SKSE::log::info("Change-hotkey popup queued for '{}' / '{}' ({})",
            record.owner, DisplayAction(record), record.binding);
    }

    void RenderScaledLine(const char* text, const float relativeScale, const ImVec4 color)
    {
        ImGui::SetWindowFontScale(g_uiScale * relativeScale);
        ImGui::PushStyleColor(ImGuiCol_Text, color);
        ImGui::TextUnformatted(text);
        ImGui::PopStyleColor();
        ImGui::SetWindowFontScale(g_uiScale);
    }

    void RenderScaledBoldLine(const char* text, const float relativeScale, const ImVec4 color)
    {
        ImGui::SetWindowFontScale(g_uiScale * relativeScale);
        ImVec2 textPosition{};
        ImGui::GetCursorScreenPos(&textPosition);
        ImGui::PushStyleColor(ImGuiCol_Text, color);
        ImGui::TextUnformatted(text);
        ImGui::PopStyleColor();
        const auto length = std::char_traits<char>::length(text);
        ImGui::ImDrawListManager::AddText_FontPtr(ImGui::GetWindowDrawList(), ImGui::GetFont(),
            ImGui::GetFontSize(), ImVec2(textPosition.x + 0.75F, textPosition.y), ImGui::GetColorU32(color),
            text, text + length, 0.0F, nullptr);
        ImGui::SetWindowFontScale(g_uiScale);
    }

    void RenderSectionHeading(const char* title, const char* description = nullptr)
    {
        ImGui::Spacing();
        RenderScaledLine(title, 1.22F, ImVec4(0.98F, 0.99F, 1.0F, 1.0F));
        if (description && description[0] != '\0') {
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.94F, 0.96F, 0.99F, 1.0F));
            ImGui::TextWrapped("%s", description);
            ImGui::PopStyleColor();
        }
        ImGui::Spacing();
    }

    ImVec2 MeasureText(const char* text)
    {
        ImVec2 measured{};
        ImGui::CalcTextSize(&measured, text, nullptr, false, -1.0F);
        return measured;
    }

    void RenderCenteredTextLine(const std::string_view text, const ImVec4* color = nullptr)
    {
        const std::string line(text);
        const auto measured = MeasureText(line.c_str());
        ImVec2 available{};
        ImGui::GetContentRegionAvail(&available);
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() +
            (std::max)(0.0F, (available.x - measured.x) * 0.5F));
        if (color) ImGui::PushStyleColor(ImGuiCol_Text, *color);
        ImGui::TextUnformatted(line.c_str());
        if (color) ImGui::PopStyleColor();
    }

    void RenderCenteredTextLines(const std::string_view text, const ImVec4* color = nullptr)
    {
        std::size_t start{};
        while (start <= text.size()) {
            const auto end = text.find('\n', start);
            const auto line = text.substr(start,
                end == std::string_view::npos ? text.size() - start : end - start);
            RenderCenteredTextLine(line, color);
            if (end == std::string_view::npos) break;
            start = end + 1U;
        }
    }

    ImVec2 ScaledButtonSize(const char* label, const float minimumWidth = 0.0F,
        const float minimumHeight = 0.0F)
    {
        const auto text = MeasureText(label);
        const auto* style = ImGui::GetStyle();
        const float horizontalPadding = style ? style->FramePadding.x * 2.0F : 16.0F * g_uiScale;
        const float verticalPadding = style ? style->FramePadding.y * 2.0F : 10.0F * g_uiScale;
        return ImVec2((std::max)(minimumWidth * g_uiScale, text.x + horizontalPadding + 8.0F * g_uiScale),
            (std::max)(minimumHeight * g_uiScale, text.y + verticalPadding + 2.0F * g_uiScale));
    }

    void RenderOverviewShortcutBadge(const UHI::OpeningHotkey& hotkey)
    {
        const std::string name = "Universal Hotkey Manager";
        const std::string prefix = name + " - ";
        const std::string shortcut = UHI::FormatOpeningHotkey(hotkey);
        const float fontSize = ImGui::GetFontSize() * 1.08F;
        ImVec2 prefixSize{};
        ImVec2 shortcutSize{};
        ImGui::ImFontManger::CalcTextSizeA(&prefixSize, ImGui::GetFont(), fontSize,
            (std::numeric_limits<float>::max)(), 0.0F,
            prefix.c_str(), prefix.c_str() + prefix.size(), nullptr);
        ImGui::ImFontManger::CalcTextSizeA(&shortcutSize, ImGui::GetFont(), fontSize,
            (std::numeric_limits<float>::max)(), 0.0F,
            shortcut.c_str(), shortcut.c_str() + shortcut.size(), nullptr);

        ImVec2 windowPosition{};
        ImVec2 windowSize{};
        ImGui::GetWindowPos(&windowPosition);
        ImGui::GetWindowSize(&windowSize);
        const float horizontalPadding = 17.0F * g_uiScale;
        const float verticalPadding = 9.0F * g_uiScale;
        const float rightMargin = 28.0F * g_uiScale;
        const float bottomMargin = 30.0F * g_uiScale;
        const ImVec2 badgeSize(prefixSize.x + shortcutSize.x + horizontalPadding * 2.0F,
            (std::max)(prefixSize.y, shortcutSize.y) + verticalPadding * 2.0F);
        const ImVec2 maximum(windowPosition.x + windowSize.x - rightMargin,
            windowPosition.y + windowSize.y - bottomMargin);
        const ImVec2 minimum(maximum.x - badgeSize.x, maximum.y - badgeSize.y);
        const float rounding = badgeSize.y * 0.30F;
        auto* draw = ImGui::GetWindowDrawList();
        ImGui::ImDrawListManager::AddRectFilled(draw, minimum, maximum,
            ImGui::GetColorU32(ImVec4(0.08F, 0.43F, 0.92F, 0.98F)), rounding, 0);
        ImGui::ImDrawListManager::AddRect(draw, minimum, maximum,
            ImGui::GetColorU32(ImVec4(0.43F, 0.72F, 1.0F, 0.95F)), rounding, 0,
            1.0F * g_uiScale);

        const float textY = minimum.y + (badgeSize.y - (std::max)(prefixSize.y, shortcutSize.y)) * 0.5F;
        const ImVec2 prefixPosition(minimum.x + horizontalPadding, textY);
        const ImVec2 shortcutPosition(prefixPosition.x + prefixSize.x, textY);
        ImGui::ImDrawListManager::AddText_FontPtr(draw, ImGui::GetFont(), fontSize, prefixPosition,
            ImGui::GetColorU32(ImVec4(1.0F, 1.0F, 1.0F, 1.0F)),
            prefix.c_str(), prefix.c_str() + prefix.size(), 0.0F, nullptr);
        ImGui::ImDrawListManager::AddText_FontPtr(draw, ImGui::GetFont(), fontSize, shortcutPosition,
            ImGui::GetColorU32(ImVec4(1.0F, 1.0F, 1.0F, 1.0F)),
            shortcut.c_str(), shortcut.c_str() + shortcut.size(), 0.0F, nullptr);
    }

    const char* CategoryDescription(const UHI::HotkeyCategory category)
    {
        switch (category) {
        case UHI::HotkeyCategory::game:
            return UiText("Built-in Skyrim controls and active loose controlmap overrides.",
                "스카이림 기본 조작과 활성 loose controlmap 덮어쓰기를 표시합니다.",
                "显示天际原生控制和当前生效的 loose controlmap 覆盖。");
        case UHI::HotkeyCategory::environment:
            return UiText("Movement, camera, world interaction, survival, and environment controls.",
                "이동, 카메라, 월드 상호작용, 생존 및 환경 조작입니다.",
                "移动、镜头、世界交互、生存与环境控制。");
        case UHI::HotkeyCategory::character:
            return UiText("Character, inventory, equipment, follower, appearance, and utility controls.",
                "캐릭터, 인벤토리, 장비, 동료, 외형 및 편의 기능입니다.",
                "角色、物品栏、装备、随从、外观与实用功能。");
        case UHI::HotkeyCategory::combat:
            return UiText("Attack, defense, targeting, stance, and modded combat-system controls.",
                "공격, 방어, 타게팅, 자세 및 모드 전투 시스템 조작입니다.",
                "攻击、防御、锁定、姿态与模组战斗系统控制。");
        case UHI::HotkeyCategory::sexlab:
            return UiText("SexLab-family framework and animation controls. This tab appears only when detected.",
                "SexLab 계열 프레임워크와 애니메이션 조작입니다. 설치가 감지될 때만 표시됩니다.",
                "SexLab 系框架与动画控制。仅在检测到安装时显示。");
        case UHI::HotkeyCategory::interface:
            return UiText("Menus, HUD widgets, overlays, notifications, and interface utilities.",
                "메뉴, HUD 위젯, 오버레이, 알림 및 인터페이스 편의 기능입니다.",
                "菜单、HUD 小部件、覆盖层、通知与界面工具。");
        case UHI::HotkeyCategory::external:
            return UiText("ReShade, Community Shaders, ENB, capture tools, and other external overlays.",
                "ReShade, Community Shaders, ENB, 캡처 도구 및 외부 오버레이입니다.",
                "ReShade、Community Shaders、ENB、捕获工具及其他外部覆盖层。");
        default:
            return UiText("Detected hotkeys grouped by purpose.", "감지한 단축키를 기능별로 분류합니다.",
                "按用途整理检测到的快捷键。");
        }
    }

    const char* CategoryLabel(const UHI::HotkeyCategory category)
    {
        switch (category) {
        case UHI::HotkeyCategory::game: return UiText("Game", "게임 기본", "游戏基础");
        case UHI::HotkeyCategory::environment: return UiText("Environment", "게임 환경", "游戏环境");
        case UHI::HotkeyCategory::character: return UiText("Character", "캐릭터", "角色");
        case UHI::HotkeyCategory::combat: return UiText("Combat", "전투", "战斗");
        case UHI::HotkeyCategory::sexlab: return "SexLab";
        case UHI::HotkeyCategory::interface: return UiText("Interface", "인터페이스", "界面");
        case UHI::HotkeyCategory::external: return UiText("External Tools", "외부 도구", "外部工具");
        default: return UiText("All", "전체", "全部");
        }
    }

    ImVec4 CategoryKeyColor(const UHI::HotkeyCategory category, const float alpha = 1.0F)
    {
        switch (category) {
        case UHI::HotkeyCategory::game: return ImVec4(0.22F, 0.34F, 0.50F, alpha);
        case UHI::HotkeyCategory::environment: return ImVec4(0.13F, 0.42F, 0.31F, alpha);
        case UHI::HotkeyCategory::character: return ImVec4(0.35F, 0.29F, 0.53F, alpha);
        case UHI::HotkeyCategory::combat: return ImVec4(0.52F, 0.24F, 0.16F, alpha);
        case UHI::HotkeyCategory::sexlab: return ImVec4(0.50F, 0.22F, 0.43F, alpha);
        case UHI::HotkeyCategory::interface: return ImVec4(0.52F, 0.36F, 0.12F, alpha);
        case UHI::HotkeyCategory::external: return ImVec4(0.10F, 0.40F, 0.53F, alpha);
        default: return ImVec4(0.24F, 0.27F, 0.31F, alpha);
        }
    }

    ImVec4 CategoryTextColor(const UHI::HotkeyCategory category, const float alpha = 1.0F)
    {
        switch (category) {
        case UHI::HotkeyCategory::game: return ImVec4(0.55F, 0.74F, 0.98F, alpha);
        case UHI::HotkeyCategory::environment: return ImVec4(0.38F, 0.92F, 0.67F, alpha);
        case UHI::HotkeyCategory::character: return ImVec4(0.73F, 0.64F, 1.0F, alpha);
        case UHI::HotkeyCategory::combat: return ImVec4(1.0F, 0.61F, 0.43F, alpha);
        case UHI::HotkeyCategory::sexlab: return ImVec4(1.0F, 0.56F, 0.84F, alpha);
        case UHI::HotkeyCategory::interface: return ImVec4(1.0F, 0.78F, 0.38F, alpha);
        case UHI::HotkeyCategory::external: return ImVec4(0.39F, 0.86F, 1.0F, alpha);
        default: return ImVec4(0.78F, 0.82F, 0.88F, alpha);
        }
    }

    ImVec4 BindingBorderColor(const UHI::HotkeyViewEntry* binding, const float alpha = 1.0F)
    {
        if (!binding) return ImVec4(0.42F, 0.46F, 0.52F, alpha * 0.65F);
        if (binding->conflict) return ImVec4(1.0F, 0.38F, 0.32F, alpha);
        return CategoryTextColor(binding->category, alpha * 0.70F);
    }

    const char* SourceTypeLabel(const UHI::HotkeyRecord& record)
    {
        const auto& detector = record.detector;
        if (detector == "BuiltInHotkeyTable")
            return UiText("Built-in controls", "기본 조작표", "内置控制表");
        if (detector == "ControlMapScanner") {
            auto filename = UHI::PathToUtf8(record.evidencePath.filename());
            std::ranges::transform(filename, filename.begin(), [](const unsigned char character) {
                return static_cast<char>(std::tolower(character));
            });
            if (filename == "controlmap_custom.txt") return "ControlMap_Custom.txt";
            if (record.stage == UHI::ScanStage::runtime && !record.editable)
                return "Runtime ControlMap";
            return "controlmap.txt";
        }
        if (detector == "StructuredConfigScanner")
            return UiText("Configuration file", "설정 파일", "配置文件");
        if (detector == "PexScanner" || detector == "PexBytecodeScanner") return "Papyrus PEX";
        if (detector == "PapyrusRuntimeProperty")
            return UiText("Live Papyrus value", "실시간 Papyrus 값", "实时 Papyrus 值");
        if (detector == "DllScanner" || detector == "DllDisassemblyScanner")
            return UiText("Native DLL", "네이티브 DLL", "原生 DLL");
        if (detector == "ActiveInputSinkAnalyzer")
            return UiText("Active CommonLib input handler", "활성 CommonLib 입력 핸들러", "活动 CommonLib 输入处理器");
        if (detector == "UserDataScanner")
            return UiText("Saved runtime data", "저장된 런타임 데이터", "已保存运行时数据");
        if (detector == "ReShadeScanner") return UiText("ReShade config", "ReShade 설정", "ReShade 配置");
        if (detector == "EnbScanner") return UiText("ENB config", "ENB 설정", "ENB 配置");
        if (detector == "CommunityShadersScanner")
            return UiText("Community Shaders config", "Community Shaders 설정", "Community Shaders 配置");
        return detector.empty() ? UiText("Unknown", "불명", "未知") : detector.c_str();
    }

    const char* LocalizedScanStatus(const std::string_view status)
    {
        if (status == "Waiting") return UiText("Waiting", "대기 중", "等待中");
        if (status == "Preparing") return UiText("Preparing", "준비 중", "准备中");
        if (status == "Previous scan restored") return UiText("Previous scan restored", "이전 스캔 복원됨", "已恢复上次扫描");
        if (status == "Cancelling...") return UiText("Cancelling...", "취소 중...", "正在取消...");
        if (status == "Cancelled") return UiText("Cancelled", "취소됨", "已取消");
        if (status == "Complete") return UiText("Complete", "완료", "完成");
        if (status == "Scan failed; see UHI log") return UiText("Scan failed; see UHI log", "스캔 실패: UHI 로그를 확인하세요", "扫描失败，请查看 UHI 日志");
        if (status == "Unable to start scan worker") return UiText("Unable to start scan worker", "스캔 작업을 시작할 수 없습니다", "无法启动扫描任务");
        if (status == "Configuration & archives") return UiText("Configuration & archives", "설정·구성·아카이브", "配置、设置与归档");
        if (status == "Scripts") return UiText("Scripts", "Papyrus 스크립트·PEX", "Papyrus 脚本与 PEX");
        if (status == "Native plugins") return UiText("Native plugins", "네이티브 플러그인·DLL", "原生插件与 DLL");
        if (status == "Saved & runtime registrations") return UiText("Saved & runtime registrations",
            "저장된 등록·외부 도구", "已保存注册与外部工具");
        return status.data();
    }

    struct KeySpec
    {
        const char* label;
        float width{ 1.0F };
        const char* displayLabel{};
    };

    std::string_view PhysicalKey(std::string_view binding)
    {
        if (const auto separator = binding.rfind('+'); separator != std::string_view::npos) {
            binding.remove_prefix(separator + 1);
        }
        return binding;
    }

    std::string FriendlyBindingLabel(const std::string_view binding)
    {
        const auto physical = PhysicalKey(binding);
        std::string_view friendly = physical;
        if (physical == "LMB") friendly = "Left Click";
        else if (physical == "RMB") friendly = "Right Click";
        else if (physical == "MMB") friendly = "Middle Click";
        else if (physical == "M4") friendly = "Side BT DN";
        else if (physical == "M5") friendly = "Side BT UP";
        else if (physical == "DUp") friendly = "D-pad UP";
        else if (physical == "DDown") friendly = "D-pad DN";
        else if (physical == "DLeft") friendly = "D-pad LT";
        else if (physical == "DRight") friendly = "D-pad RT";
        else if (physical == "Left Stick Click") friendly = "LS Click";
        else if (physical == "Right Stick Click") friendly = "RS Click";
        if (friendly == physical) return std::string(binding);

        const auto separator = binding.rfind('+');
        return separator == std::string_view::npos ? std::string(friendly) :
            std::string(binding.substr(0, separator + 1U)) + std::string(friendly);
    }

    const char* LocalizedDeviceButtonName(const std::string_view physical, const char* fallback)
    {
        if (physical == "LMB") return "Left Click";
        if (physical == "RMB") return "Right Click";
        if (physical == "MMB") return "Middle Click";
        if (physical == "M4") return "Side BT DN";
        if (physical == "M5") return "Side BT UP";
        // Physical device labels are standard input names, not UI prose. Keep
        // them stable across UI languages so a controller diagram always uses
        // the familiar English notation.
        if (physical == "DUp") return "D-pad UP";
        if (physical == "DDown") return "D-pad DN";
        if (physical == "DLeft") return "D-pad LT";
        if (physical == "DRight") return "D-pad RT";
        if (physical == "Left Stick Click") return "LS Click";
        if (physical == "Right Stick Click") return "RS Click";
        if (physical == "Back") return "Back";
        if (physical == "Start") return "Start";
        return fallback;
    }

    std::optional<std::uint32_t> DirectInputFromImGuiKey(const ImGuiKey key) noexcept
    {
        static constexpr std::array<std::uint32_t, 10> digitCodes{
            0x0BU, 0x02U, 0x03U, 0x04U, 0x05U, 0x06U, 0x07U, 0x08U, 0x09U, 0x0AU
        };
        static constexpr std::array<std::uint32_t, 26> letterCodes{
            0x1EU, 0x30U, 0x2EU, 0x20U, 0x12U, 0x21U, 0x22U, 0x23U, 0x17U,
            0x24U, 0x25U, 0x26U, 0x32U, 0x31U, 0x18U, 0x19U, 0x10U, 0x13U,
            0x1FU, 0x14U, 0x16U, 0x2FU, 0x11U, 0x2DU, 0x15U, 0x2CU
        };
        if (key >= ImGuiKey_0 && key <= ImGuiKey_9)
            return digitCodes[static_cast<std::size_t>(key - ImGuiKey_0)];
        if (key >= ImGuiKey_A && key <= ImGuiKey_Z)
            return letterCodes[static_cast<std::size_t>(key - ImGuiKey_A)];
        if (key >= ImGuiKey_F1 && key <= ImGuiKey_F10)
            return 0x3BU + static_cast<std::uint32_t>(key - ImGuiKey_F1);
        if (key == ImGuiKey_F11) return 0x57U;
        if (key == ImGuiKey_F12) return 0x58U;
        if (key >= ImGuiKey_Keypad0 && key <= ImGuiKey_Keypad9) {
            static constexpr std::array<std::uint32_t, 10> keypadCodes{
                0x52U, 0x4FU, 0x50U, 0x51U, 0x4BU, 0x4CU, 0x4DU, 0x47U, 0x48U, 0x49U
            };
            return keypadCodes[static_cast<std::size_t>(key - ImGuiKey_Keypad0)];
        }
        switch (key) {
        case ImGuiKey_Tab: return 0x0FU;
        case ImGuiKey_LeftArrow: return 0xCBU;
        case ImGuiKey_RightArrow: return 0xCDU;
        case ImGuiKey_UpArrow: return 0xC8U;
        case ImGuiKey_DownArrow: return 0xD0U;
        case ImGuiKey_PageUp: return 0xC9U;
        case ImGuiKey_PageDown: return 0xD1U;
        case ImGuiKey_Home: return 0xC7U;
        case ImGuiKey_End: return 0xCFU;
        case ImGuiKey_Insert: return 0xD2U;
        case ImGuiKey_Delete: return 0xD3U;
        case ImGuiKey_Backspace: return 0x0EU;
        case ImGuiKey_Space: return 0x39U;
        case ImGuiKey_Enter: return 0x1CU;
        case ImGuiKey_LeftSuper: return 0xDBU;
        case ImGuiKey_RightSuper: return 0xDCU;
        case ImGuiKey_Menu: return 0xDDU;
        case ImGuiKey_Apostrophe: return 0x28U;
        case ImGuiKey_Comma: return 0x33U;
        case ImGuiKey_Minus: return 0x0CU;
        case ImGuiKey_Period: return 0x34U;
        case ImGuiKey_Slash: return 0x35U;
        case ImGuiKey_Semicolon: return 0x27U;
        case ImGuiKey_Equal: return 0x0DU;
        case ImGuiKey_LeftBracket: return 0x1AU;
        case ImGuiKey_Backslash: return 0x2BU;
        case ImGuiKey_RightBracket: return 0x1BU;
        case ImGuiKey_GraveAccent: return 0x29U;
        case ImGuiKey_CapsLock: return 0x3AU;
        case ImGuiKey_ScrollLock: return 0x46U;
        case ImGuiKey_NumLock: return 0x45U;
        case ImGuiKey_PrintScreen: return 0xB7U;
        case ImGuiKey_Pause: return 0xC5U;
        case ImGuiKey_KeypadDecimal: return 0x53U;
        case ImGuiKey_KeypadDivide: return 0xB5U;
        case ImGuiKey_KeypadMultiply: return 0x37U;
        case ImGuiKey_KeypadSubtract: return 0x4AU;
        case ImGuiKey_KeypadAdd: return 0x4EU;
        case ImGuiKey_KeypadEnter: return 0x9CU;
        default: return std::nullopt;
        }
    }

    bool PollImGuiBindingCapture()
    {
        if (!g_bindingCaptureActive.load() || ImGui::GetFrameCount() <= g_bindingCaptureArmedFrame)
            return false;
        if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
            UHI::CancelMenuFrameworkBindingCapture();
            return true;
        }
        static constexpr std::pair<ImGuiKey, std::uint32_t> modifiers[]{
            { ImGuiKey_LeftCtrl, 0x1DU }, { ImGuiKey_RightCtrl, 0x9DU },
            { ImGuiKey_LeftShift, 0x2AU }, { ImGuiKey_RightShift, 0x36U },
            { ImGuiKey_LeftAlt, 0x38U }, { ImGuiKey_RightAlt, 0xB8U }
        };
        std::uint32_t keyboardModifier{};
        for (const auto& [key, code] : modifiers) {
            if (ImGui::IsKeyDown(key)) {
                keyboardModifier = code;
                break;
            }
        }
        for (int value = ImGuiKey_NamedKey_BEGIN; value < ImGuiKey_GamepadStart; ++value) {
            const auto key = static_cast<ImGuiKey>(value);
            const auto code = DirectInputFromImGuiKey(key);
            if (code && ImGui::IsKeyPressed(key, false)) {
                return UHI::CaptureMenuFrameworkBindingInput("keyboard", *code,
                    keyboardModifier == 0U ? "" : "keyboard", keyboardModifier);
            }
        }
        for (std::uint32_t code = 0; code < 5U; ++code) {
            if (ImGui::IsMouseClicked(static_cast<ImGuiMouseButton>(code), false)) {
                return UHI::CaptureMenuFrameworkBindingInput("mouse", code,
                    keyboardModifier == 0U ? "" : "keyboard", keyboardModifier);
            }
        }
        static constexpr std::pair<ImGuiKey, std::uint32_t> gamepadKeys[]{
            { ImGuiKey_GamepadDpadUp, 266U }, { ImGuiKey_GamepadDpadDown, 267U },
            { ImGuiKey_GamepadDpadLeft, 268U }, { ImGuiKey_GamepadDpadRight, 269U },
            { ImGuiKey_GamepadStart, 270U }, { ImGuiKey_GamepadBack, 271U },
            { ImGuiKey_GamepadL3, 272U }, { ImGuiKey_GamepadR3, 273U },
            { ImGuiKey_GamepadL1, 274U }, { ImGuiKey_GamepadR1, 275U },
            { ImGuiKey_GamepadFaceDown, 276U }, { ImGuiKey_GamepadFaceRight, 277U },
            { ImGuiKey_GamepadFaceLeft, 278U }, { ImGuiKey_GamepadFaceUp, 279U },
            { ImGuiKey_GamepadL2, 280U }, { ImGuiKey_GamepadR2, 281U }
        };
        std::uint32_t gamepadModifier{};
        for (const auto& [key, code] : gamepadKeys) {
            if ((code == 271U || code == 274U || code == 275U || code == 280U || code == 281U) &&
                ImGui::IsKeyDown(key)) {
                gamepadModifier = code;
                break;
            }
        }
        for (const auto& [key, code] : gamepadKeys) {
            if (code != gamepadModifier && ImGui::IsKeyPressed(key, false)) {
                return UHI::CaptureMenuFrameworkBindingInput("gamepad", code,
                    gamepadModifier == 0U ? "" : "gamepad", gamepadModifier);
            }
        }
        for (const auto& [key, code] : modifiers) {
            if (ImGui::IsKeyReleased(key))
                return UHI::CaptureMenuFrameworkBindingInput("keyboard", code, "", 0U);
        }
        for (const auto& [key, code] : gamepadKeys) {
            if ((code == 271U || code == 274U || code == 275U || code == 280U || code == 281U) &&
                ImGui::IsKeyReleased(key))
                return UHI::CaptureMenuFrameworkBindingInput("gamepad", code, "", 0U);
        }
        return false;
    }

    bool PollImGuiOpeningHotkeyCapture()
    {
        if (!UHI::IsMenuFrameworkOpeningHotkeyCaptureActive() ||
            ImGui::GetFrameCount() <= g_openingHotkeyCaptureArmedFrame) return false;
        if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
            UHI::CancelMenuFrameworkOpeningHotkeyCapture();
            return true;
        }
        const std::uint32_t ctrl = ImGui::IsKeyDown(ImGuiKey_RightCtrl) ? 0x9DU :
            ImGui::IsKeyDown(ImGuiKey_LeftCtrl) ? 0x1DU : 0U;
        const std::uint32_t shift = ImGui::IsKeyDown(ImGuiKey_RightShift) ? 0x36U :
            ImGui::IsKeyDown(ImGuiKey_LeftShift) ? 0x2AU : 0U;
        const std::uint32_t alt = ImGui::IsKeyDown(ImGuiKey_RightAlt) ? 0xB8U :
            ImGui::IsKeyDown(ImGuiKey_LeftAlt) ? 0x38U : 0U;
        for (int value = ImGuiKey_NamedKey_BEGIN; value < ImGuiKey_GamepadStart; ++value) {
            const auto key = static_cast<ImGuiKey>(value);
            const auto code = DirectInputFromImGuiKey(key);
            if (!code || *code == ctrl || *code == shift || *code == alt) continue;
            if (ImGui::IsKeyPressed(key, false)) {
                return UHI::CaptureMenuFrameworkOpeningHotkey(*code, ctrl, shift, alt);
            }
        }
        return false;
    }

    void HandleBindingHotspot(const UHI::HotkeyViewEntry* binding,
        const ImVec2 minimum, const ImVec2 maximum)
    {
        // The context menu is rendered after the device map.  Without this
        // guard, the left click used to choose a context-menu command also
        // reached the key rectangle behind the menu and started a rename.
        const int frame = ImGui::GetFrameCount();
        if (g_modalInputActive.load() || g_editorModal != EditorModal::none ||
            g_bindingContextPopupActive || frame <= g_bindingHotspotBlockedUntilFrame ||
            ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId)) return;
        if (!binding || !binding->record || !ImGui::IsMouseHoveringRect(minimum, maximum, true)) return;
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Left, false)) BeginActionRename(*binding->record);
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Right, false)) {
            g_contextRecord = *binding->record;
            g_openBindingContextPopup = true;
            g_bindingContextPopupActive = true;
            g_modalInputActive = true;
            g_bindingHotspotBlockedUntilFrame = frame + 2;
        }
    }

    using BindingLookup = std::unordered_map<std::string_view, const UHI::HotkeyViewEntry*>;

    int OverviewDisplayPriority(const UHI::HotkeyCategory category) noexcept
    {
        switch (category) {
        case UHI::HotkeyCategory::external: return 7;
        case UHI::HotkeyCategory::interface: return 6;
        case UHI::HotkeyCategory::sexlab: return 5;
        case UHI::HotkeyCategory::combat: return 4;
        case UHI::HotkeyCategory::character: return 3;
        case UHI::HotkeyCategory::environment: return 2;
        case UHI::HotkeyCategory::game: return 1;
        default: return 0;
        }
    }

    BindingLookup BuildBindingLookup(const std::vector<UHI::HotkeyViewEntry>& entries)
    {
        BindingLookup lookup;
        lookup.reserve(entries.size());
        for (const auto& entry : entries) {
            const auto physical = PhysicalKey(entry.displayBinding);
            const auto found = lookup.find(physical);
            if (found == lookup.end() ||
                OverviewDisplayPriority(entry.category) > OverviewDisplayPriority(found->second->category) ||
                (OverviewDisplayPriority(entry.category) == OverviewDisplayPriority(found->second->category) &&
                    entry.conflict && !found->second->conflict)) {
                lookup.insert_or_assign(physical, &entry);
            }
        }
        return lookup;
    }

    void RenderPhysicalKey(const KeySpec& key, const BindingLookup& bindings)
    {
        const auto found = bindings.find(key.label);
        const auto* binding = found == bindings.end() ? nullptr : found->second;
        const auto color = ImVec4(0.025F, 0.030F, 0.040F, 0.98F);
        ImGui::PushStyleColor(ImGuiCol_Button, color);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.07F, 0.08F, 0.10F, 1.0F));
        ImGui::PushStyleColor(ImGuiCol_Border, BindingBorderColor(binding));
        ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize,
            binding && binding->conflict ? 1.25F : 0.65F);
        ImGui::PushID(key.label);
        ImGui::Button(key.displayLabel ? key.displayLabel : key.label,
            ImVec2(34.0F * key.width, 32.0F));
        ImGui::PopID();
        ImGui::PopStyleVar();
        ImGui::PopStyleColor(3);
        if (binding && ImGui::IsItemHovered()) {
            const auto& record = *binding->record;
            ImGui::BeginTooltip();
            const auto action = DisplayAction(record);
            ImGui::TextUnformatted(action.c_str());
            ImGui::Text("%s: %s", UiText("Owner", "모드", "所属模组"), record.owner.c_str());
            ImGui::Text("%s: %s", UiText("Category", "카테고리", "类别"), CategoryLabel(binding->category));
            ImGui::Text("%s: %s", UiText("Binding", "단축키", "快捷键"), record.binding.c_str());
            ImGui::Text("%s: %s", UiText("Source type", "출처 유형", "来源类型"), SourceTypeLabel(record));
            ImGui::Text("%s: %s (%s)", UiText("Active in", "활성 컨텍스트", "生效上下文"),
                LocalizedContextLabel(record.contextMask).c_str(),
                ContextConfidenceName(record.contextConfidence));
            ImGui::Text("%s: %s%s", UiText("Status", "상태", "状态"),
                record.editable ? UiText("editable", "변경 가능", "可修改") : UiText("read-only", "읽기 전용", "只读"),
                binding->conflict ? UiText(" | CONFIRMED CONFLICT", " | 확정 충돌", " | 已确认冲突") :
                binding->conditionalConflict ? UiText(" | OVERLAP", " | 중복", " | 重叠") : "");
            ImGui::EndTooltip();
        }
    }

    void RenderKeyboardBoard(const std::vector<UHI::HotkeyViewEntry>& entries)
    {
        const auto bindings = BuildBindingLookup(entries);
        static constexpr KeySpec row1[] = { { "Esc" }, { "F1" }, { "F2" }, { "F3" }, { "F4" },
            { "F5" }, { "F6" }, { "F7" }, { "F8" }, { "F9" }, { "F10" }, { "F11" }, { "F12" },
            { "PrtSc" }, { "ScrLk" }, { "Pause" } };
        static constexpr KeySpec row2[] = { { "`" }, { "1" }, { "2" }, { "3" }, { "4" }, { "5" },
            { "6" }, { "7" }, { "8" }, { "9" }, { "0" }, { "-" }, { "=" }, { "Bksp", 2.0F },
            { "Ins" }, { "Home" }, { "PgUp" }, { "NumLk" }, { "Num/" }, { "Num*" }, { "Num-" } };
        static constexpr KeySpec row3[] = { { "Tab", 1.5F }, { "Q" }, { "W" }, { "E" }, { "R" }, { "T" },
            { "Y" }, { "U" }, { "I" }, { "O" }, { "P" }, { "[" }, { "]" }, { "\\", 1.5F },
            { "Del" }, { "End" }, { "PgDn" }, { "Num7" }, { "Num8" }, { "Num9" }, { "Num+" } };
        static constexpr KeySpec row4[] = { { "Caps", 1.8F }, { "A" }, { "S" }, { "D" }, { "F" }, { "G" },
            { "H" }, { "J" }, { "K" }, { "L" }, { ";" }, { "'" }, { "Ent", 2.2F },
            { "Num4" }, { "Num5" }, { "Num6" } };
        static constexpr KeySpec row5[] = { { "LShift", 2.2F }, { "Z" }, { "X" }, { "C" }, { "V" }, { "B" },
            { "N" }, { "M" }, { "," }, { "." }, { "/" }, { "RShift", 2.6F }, { "Up" },
            { "Num1" }, { "Num2" }, { "Num3" }, { "NumEnt" } };
        static constexpr KeySpec row6[] = { { "LCtrl", 1.5F }, { "LWin", 1.2F }, { "LAlt", 1.2F },
            { "Space", 6.0F }, { "RAlt", 1.2F }, { "RWin", 1.2F }, { "Menu", 1.2F }, { "RCtrl", 1.5F },
            { "Left" }, { "Down" }, { "Right" }, { "Num0", 2.0F }, { "Num." } };
        const auto renderRow = [&](const auto& row) {
            for (std::size_t index = 0; index < std::size(row); ++index) {
                if (index != 0) ImGui::SameLine();
                RenderPhysicalKey(row[index], bindings);
            }
        };
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(3.0F, 3.0F));
        renderRow(row1); renderRow(row2); renderRow(row3); renderRow(row4); renderRow(row5); renderRow(row6);
        ImGui::PopStyleVar();
    }

    void RenderMouseBoard(const std::vector<UHI::HotkeyViewEntry>& entries)
    {
        const auto bindings = BuildBindingLookup(entries);
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(4.0F, 4.0F));
        RenderPhysicalKey({ "LMB", 2.0F, "Left Click" }, bindings); ImGui::SameLine();
        RenderPhysicalKey({ "RMB", 2.0F, "Right Click" }, bindings);
        RenderPhysicalKey({ "MMB", 1.4F, "Middle Click" }, bindings); ImGui::SameLine();
        RenderPhysicalKey({ "M4", 1.4F, "Side BT DN" }, bindings); ImGui::SameLine();
        RenderPhysicalKey({ "M5", 1.4F, "Side BT UP" }, bindings);
        ImGui::PopStyleVar();
    }

    void RenderGamepadBoard(const std::vector<UHI::HotkeyViewEntry>& entries)
    {
        const auto bindings = BuildBindingLookup(entries);
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(4.0F, 4.0F));
        RenderPhysicalKey({ "LT", 1.8F }, bindings); ImGui::SameLine();
        RenderPhysicalKey({ "LB", 1.8F }, bindings); ImGui::SameLine();
        ImGui::Dummy(ImVec2(80.0F, 1.0F)); ImGui::SameLine();
        RenderPhysicalKey({ "RB", 1.8F }, bindings); ImGui::SameLine();
        RenderPhysicalKey({ "RT", 1.8F }, bindings);
        RenderPhysicalKey({ "DUp", 1.5F }, bindings); ImGui::SameLine();
        RenderPhysicalKey({ "DDown", 1.5F }, bindings); ImGui::SameLine();
        RenderPhysicalKey({ "DLeft", 1.5F }, bindings); ImGui::SameLine();
        RenderPhysicalKey({ "DRight", 1.5F }, bindings); ImGui::SameLine();
        RenderPhysicalKey({ "Left Stick", 1.6F, "LS" }, bindings); ImGui::SameLine();
        RenderPhysicalKey({ "Left Stick Click", 2.2F, "LS Click" }, bindings); ImGui::SameLine();
        RenderPhysicalKey({ "Back", 1.5F }, bindings); ImGui::SameLine();
        RenderPhysicalKey({ "Start", 1.5F }, bindings); ImGui::SameLine();
        RenderPhysicalKey({ "Right Stick", 1.6F, "RS" }, bindings); ImGui::SameLine();
        RenderPhysicalKey({ "Right Stick Click", 2.2F, "RS Click" }, bindings); ImGui::SameLine();
        RenderPhysicalKey({ "X", 1.2F }, bindings); ImGui::SameLine();
        RenderPhysicalKey({ "Y", 1.2F }, bindings); ImGui::SameLine();
        RenderPhysicalKey({ "A", 1.2F }, bindings); ImGui::SameLine();
        RenderPhysicalKey({ "B", 1.2F }, bindings);
        ImGui::PopStyleVar();
    }

    struct VectorKeySpec
    {
        const char* label;
        float x;
        float y;
        float width{ 1.0F };
        float height{ 1.0F };
        const char* displayLabel{};
    };

    ImU32 VectorKeyColor(const UHI::HotkeyViewEntry* binding)
    {
        // Device-view keys use one restrained card treatment: black fill for
        // both empty and mapped keys, with category/confirmed-conflict state
        // communicated by the outline instead of a large colored surface.
        (void)binding;
        return ImGui::GetColorU32(ImVec4(0.0F, 0.0F, 0.0F, 1.0F));
    }

    std::vector<const UHI::HotkeyViewEntry*> TooltipBindings(const UHI::HotkeyViewEntry& selected)
    {
        std::vector<const UHI::HotkeyViewEntry*> result;
        const auto selectedPhysical = PhysicalKey(selected.displayBinding);
        const auto selectedDevice = selected.record ? selected.record->device : std::string{};
        std::unordered_set<std::string> seen;
        for (const auto& group : g_filteredGroups) {
            if (!selectedDevice.empty() && group.device != selectedDevice) continue;
            for (const auto& candidate : group.entries) {
                if (!candidate.record || PhysicalKey(candidate.displayBinding) != selectedPhysical) continue;
                const auto identity = ActionIdentity(*candidate.record);
                if (seen.insert(identity).second) result.push_back(&candidate);
            }
        }
        if (result.empty()) result.push_back(&selected);
        std::stable_sort(result.begin(), result.end(), [&](const auto* left, const auto* right) {
            if (left == right) return false;
            if (left == &selected) return true;
            if (right == &selected) return false;
            const auto leftPriority = OverviewDisplayPriority(left->category);
            const auto rightPriority = OverviewDisplayPriority(right->category);
            if (leftPriority != rightPriority) return leftPriority > rightPriority;
            return ActionIdentity(*left->record) < ActionIdentity(*right->record);
        });
        return result;
    }

    void TooltipTextValue(const char* label, const std::string_view value,
        const ImVec4* valueColor = nullptr)
    {
        ImGui::Text("%s:", label);
        ImGui::SameLine(0.0F, 5.0F * g_uiScale);
        if (valueColor) ImGui::TextColored(*valueColor, "%.*s", static_cast<int>(value.size()), value.data());
        else ImGui::TextUnformatted(value.data(), value.data() + value.size());
    }

    const char* LocalizedDeviceType(const std::string_view device)
    {
        if (device == "keyboard") return UiText("keyboard", "키보드", "键盘");
        if (device == "mouse") return UiText("mouse", "마우스", "鼠标");
        if (device == "gamepad") return UiText("gamepad", "게임패드", "手柄");
        return device.empty() ? "-" : nullptr;
    }

    void RenderBindingTooltipEntry(const UHI::HotkeyViewEntry& entry, const bool detailed)
    {
        if (!entry.record) return;
        const auto& record = *entry.record;
        const auto ownerColor = CategoryTextColor(entry.category, 1.0F);
        TooltipTextValue(UiText("Mod", "모드", "模组"), record.owner, &ownerColor);
        TooltipTextValue(UiText("Action", "기능", "功能"), DisplayAction(record));
        TooltipTextValue(UiText("Category", "카테고리", "类别"), CategoryLabel(entry.category));
        TooltipTextValue(UiText("Hotkey", "단축키", "快捷键"), FriendlyBindingLabel(entry.displayBinding));
        if (!detailed) return;

        const auto* localizedDevice = LocalizedDeviceType(record.device);
        TooltipTextValue(UiText("Device type", "장치 유형", "设备类型"),
            localizedDevice ? std::string_view(localizedDevice) : std::string_view(record.device));
        const auto context = LocalizedContextLabel(record.contextMask);
        const std::string contextWithConfidence = context + " (" +
            ContextConfidenceName(record.contextConfidence) + ")";
        TooltipTextValue(UiText("Active in", "활성 컨텍스트", "生效上下文"), contextWithConfidence);
        std::string status = record.editable ? UiText("editable", "변경 가능", "可修改") :
            UiText("read-only", "읽기 전용", "只读");
        if (entry.conflict) status += UiText(" | confirmed conflict", " | 확정 충돌", " | 已确认冲突");
        else if (entry.conditionalConflict)
            status += UiText(" | overlap", " | 중복", " | 重叠");
        TooltipTextValue(UiText("Status", "상태", "状态"), status);
    }

    void VectorBindingTooltip(const UHI::HotkeyViewEntry* binding)
    {
        // Modal editors own all pointer interaction. Do not let a device card
        // underneath them leak a rollover tooltip through the popup surface.
        if (g_modalInputActive.load() || g_editorModal != EditorModal::none ||
            g_bindingContextPopupActive) return;
        if (!binding) return;
        const auto matches = TooltipBindings(*binding);
        const bool detailed = g_page == HotkeyViewPage::category;
        constexpr std::size_t maximumVisibleBindings = 3U;
        const auto visibleCount = (std::min)(maximumVisibleBindings, matches.size());
        ImGui::BeginTooltip();
        for (std::size_t index = 0; index < visibleCount; ++index) {
            if (index != 0U) ImGui::Separator();
            RenderBindingTooltipEntry(*matches[index], detailed);
        }
        if (matches.size() > visibleCount) {
            ImGui::Separator();
            ImGui::TextDisabled(UiText("+ %zu more", "+ %zu개 더 있음", "+ 另有 %zu 项"),
                matches.size() - visibleCount);
        }
        ImGui::EndTooltip();
    }

    std::string EllipsizeVectorText(const std::string_view text, const float fontSize, const float maximumWidth)
    {
        if (text.empty() || maximumWidth <= 1.0F) return {};

        ImVec2 measured{};
        ImGui::ImFontManger::CalcTextSizeA(&measured, ImGui::GetFont(), fontSize,
            (std::numeric_limits<float>::max)(), 0.0F, text.data(), text.data() + text.size(), nullptr);
        if (measured.x <= maximumWidth) return std::string(text);

        static constexpr std::string_view ellipsis = "...";
        ImVec2 ellipsisSize{};
        ImGui::ImFontManger::CalcTextSizeA(&ellipsisSize, ImGui::GetFont(), fontSize,
            (std::numeric_limits<float>::max)(), 0.0F, ellipsis.data(), ellipsis.data() + ellipsis.size(), nullptr);
        if (ellipsisSize.x >= maximumWidth) return {};

        const char* remaining = text.data();
        ImVec2 prefixSize{};
        ImGui::ImFontManger::CalcTextSizeA(&prefixSize, ImGui::GetFont(), fontSize,
            maximumWidth - ellipsisSize.x, 0.0F, text.data(), text.data() + text.size(), &remaining);
        if (remaining <= text.data()) return std::string(ellipsis);
        return std::string(text.data(), remaining) + std::string(ellipsis);
    }

    std::array<std::string, 2> WrapVectorTextTwoLines(const std::string_view text,
        const float fontSize, const float maximumWidth)
    {
        std::array<std::string, 2> lines{};
        if (text.empty()) return lines;

        ImVec2 measured{};
        ImGui::ImFontManger::CalcTextSizeA(&measured, ImGui::GetFont(), fontSize,
            (std::numeric_limits<float>::max)(), 0.0F, text.data(), text.data() + text.size(), nullptr);
        if (measured.x <= maximumWidth) {
            lines[0] = std::string(text);
            return lines;
        }

        std::size_t split = std::string_view::npos;
        for (auto position = text.find(' '); position != std::string_view::npos;
             position = text.find(' ', position + 1U)) {
            ImVec2 prefixSize{};
            ImGui::ImFontManger::CalcTextSizeA(&prefixSize, ImGui::GetFont(), fontSize,
                (std::numeric_limits<float>::max)(), 0.0F, text.data(), text.data() + position, nullptr);
            if (prefixSize.x > maximumWidth) break;
            split = position;
        }
        if (split == std::string_view::npos) {
            lines[0] = EllipsizeVectorText(text, fontSize, maximumWidth);
            return lines;
        }
        lines[0] = std::string(text.substr(0, split));
        const auto second = text.substr(split + 1U);
        lines[1] = EllipsizeVectorText(second, fontSize, maximumWidth);
        return lines;
    }

    void DrawMappedLine(ImDrawList* draw, const ImVec2 position, const float maximumWidth,
        const float fontSize, const ImVec4 color, const std::string_view text, const ImVec4& clip)
    {
        const auto visible = EllipsizeVectorText(text, fontSize, maximumWidth);
        if (visible.empty()) return;
        ImGui::ImDrawListManager::AddText_FontPtr(draw, ImGui::GetFont(), fontSize, position,
            ImGui::GetColorU32(color), visible.c_str(), visible.c_str() + visible.size(), 0.0F, &clip);
    }

    void DrawVectorBoldLabel(ImDrawList* draw, ImVec2 position, const char* label,
        float fontSize, ImVec4 color);
    float MeasureVectorText(std::string_view text, float fontSize);

    float VectorKeyNameSize(const float unit)
    {
        return (std::max)(10.0F,
            (std::max)(18.5F * g_uiScale, unit * 0.33F) - 3.0F * g_uiScale);
    }

    float VectorOwnerSize(const float unit)
    {
        return (std::max)(9.0F,
            (std::max)(14.0F * g_uiScale, unit * 0.235F) - 2.0F * g_uiScale);
    }

    float VectorActionSize(const float unit)
    {
        return (std::max)(8.0F,
            (std::max)(20.0F * g_uiScale, unit * 0.31F) - 8.0F * g_uiScale);
    }

    void DrawVectorKey(ImDrawList* draw, const ImVec2 origin, const float unit,
        const VectorKeySpec& key, const BindingLookup& bindings)
    {
        const auto found = bindings.find(key.label);
        const auto* binding = found == bindings.end() ? nullptr : found->second;
        const ImVec2 minimum(origin.x + key.x * unit, origin.y + key.y * unit);
        const ImVec2 maximum(minimum.x + key.width * unit - 2.0F,
            minimum.y + key.height * unit - 2.0F);
        const auto fill = VectorKeyColor(binding);
        const auto border = ImGui::GetColorU32(BindingBorderColor(binding, 0.92F));
        ImGui::ImDrawListManager::AddRectFilled(draw, minimum, maximum, fill, 3.0F, 0);
        ImGui::ImDrawListManager::AddRect(draw, minimum, maximum, border, 3.0F, 0,
            binding && binding->conflict ? 1.25F : 0.65F);
        // Every device card derives its three text roles from these same
        // window-width-based values. This keeps keyboard, mouse and gamepad
        // typography identical at every UI scale.
        const float keyFontSize = VectorKeyNameSize(unit);
        const float ownerFontSize = VectorOwnerSize(unit);
        const auto keyColor = ImGui::GetColorU32(ImVec4(0.56F, 0.58F, 0.62F, 0.98F));
        const ImVec2 keyTextPosition(minimum.x + 4.0F, minimum.y + 3.0F);
        const char* displayLabel = key.displayLabel ? key.displayLabel : key.label;
        const auto displayLabelLength = std::char_traits<char>::length(displayLabel);
        ImGui::ImDrawListManager::AddText_FontPtr(draw, ImGui::GetFont(), keyFontSize,
            keyTextPosition, keyColor,
            displayLabel, displayLabel + displayLabelLength, 0.0F, nullptr);
        // Menu Framework exposes one default font here. A second sub-pixel-offset
        // pass gives the compact key legends a clear bold weight without changing
        // the font used by the rest of the interface.
        ImGui::ImDrawListManager::AddText_FontPtr(draw, ImGui::GetFont(), keyFontSize,
            ImVec2(keyTextPosition.x + 0.65F, keyTextPosition.y), keyColor,
            displayLabel, displayLabel + displayLabelLength, 0.0F, nullptr);
        ImGui::ImDrawListManager::AddText_FontPtr(draw, ImGui::GetFont(), keyFontSize,
            ImVec2(keyTextPosition.x + 0.28F, keyTextPosition.y + 0.42F), keyColor,
            displayLabel, displayLabel + displayLabelLength, 0.0F, nullptr);
        if (binding) {
            ImVec2 keyLabelMeasure{};
            ImGui::ImFontManger::CalcTextSizeA(&keyLabelMeasure, ImGui::GetFont(), keyFontSize,
                (std::numeric_limits<float>::max)(), 0.0F,
                displayLabel, displayLabel + displayLabelLength, nullptr);
            const float ownerLimit = (std::max)(12.0F, maximum.x - keyTextPosition.x -
                keyLabelMeasure.x - 12.0F);
            const auto visibleOwner = EllipsizeVectorText(
                binding->record ? binding->record->owner : std::string_view{}, ownerFontSize, ownerLimit);
            const float visibleOwnerWidth = MeasureVectorText(visibleOwner, ownerFontSize);
            DrawVectorBoldLabel(draw,
                ImVec2(maximum.x - visibleOwnerWidth - 4.0F, minimum.y + 3.0F),
                visibleOwner.c_str(), ownerFontSize, CategoryTextColor(binding->category, 0.98F));
            const auto separator = binding->displayBinding.rfind('+');
            if (separator != std::string::npos && separator > 0U) {
                const float modifierSize = ownerFontSize;
                const float available = (std::max)(10.0F, maximum.x - keyTextPosition.x -
                    keyLabelMeasure.x - 12.0F);
                const auto modifier = EllipsizeVectorText(
                    std::string_view(binding->displayBinding).substr(0, separator), modifierSize, available);
                ImVec2 modifierMeasure{};
                ImGui::ImFontManger::CalcTextSizeA(&modifierMeasure, ImGui::GetFont(), modifierSize,
                    (std::numeric_limits<float>::max)(), 0.0F,
                    modifier.c_str(), modifier.c_str() + modifier.size(), nullptr);
                ImGui::ImDrawListManager::AddText_FontPtr(draw, ImGui::GetFont(), modifierSize,
                    ImVec2(maximum.x - modifierMeasure.x - 4.0F,
                        minimum.y + ownerFontSize + 5.0F),
                    ImGui::GetColorU32(CategoryTextColor(binding->category, 0.96F)),
                    modifier.c_str(), modifier.c_str() + modifier.size(), 0.0F, nullptr);
            }
        }
        if (binding && maximum.y - minimum.y > 24.0F) {
            const auto& record = *binding->record;
            // Action text is intentionally two points below the former size;
            // the key legend itself remains unchanged and visually dominant.
            const float actionSize = VectorActionSize(unit);
            const float textWidth = (std::max)(12.0F, maximum.x - minimum.x - 8.0F);
            const ImVec4 textClip(minimum.x + 3.0F, minimum.y + unit * 0.34F,
                maximum.x - 3.0F, maximum.y - 3.0F);
            const auto actionLabel = UHI::CompactActionLabel(DisplayAction(record));
            const auto actionLines = WrapVectorTextTwoLines(actionLabel, actionSize, textWidth);
            const float firstLineY = actionLines[1].empty() ? unit * 0.56F : unit * 0.39F;
            DrawMappedLine(draw, ImVec2(minimum.x + 4.0F, minimum.y + firstLineY),
                textWidth, actionSize, ImVec4(0.96F, 0.97F, 1.0F, 1.0F), actionLines[0], textClip);
            if (!actionLines[1].empty()) {
                DrawMappedLine(draw, ImVec2(minimum.x + 4.0F, minimum.y + unit * 0.68F),
                    textWidth, actionSize, ImVec4(0.96F, 0.97F, 1.0F, 1.0F), actionLines[1], textClip);
            }
        }
        if (ImGui::IsMouseHoveringRect(minimum, maximum, true)) VectorBindingTooltip(binding);
        HandleBindingHotspot(binding, minimum, maximum);
    }

    void DrawVectorKeyboard(ImDrawList* draw, const ImVec2 origin, const float width,
        const std::vector<UHI::HotkeyViewEntry>& entries)
    {
        static constexpr VectorKeySpec keys[]{
            {"Esc",0,0},{"F1",2.1F,0},{"F2",3.15F,0},{"F3",4.2F,0},{"F4",5.25F,0},
            {"F5",6.6F,0},{"F6",7.65F,0},{"F7",8.7F,0},{"F8",9.75F,0},
            {"F9",11.1F,0},{"F10",12.15F,0},{"F11",13.2F,0},{"F12",14.25F,0},
            {"PrtSc",16.2F,0},{"ScrLk",17.25F,0},{"Pause",18.3F,0},
            {"`",0,1.3F},{"1",1.05F,1.3F},{"2",2.1F,1.3F},{"3",3.15F,1.3F},{"4",4.2F,1.3F},
            {"5",5.25F,1.3F},{"6",6.3F,1.3F},{"7",7.35F,1.3F},{"8",8.4F,1.3F},{"9",9.45F,1.3F},
            {"0",10.5F,1.3F},{"-",11.55F,1.3F},{"=",12.6F,1.3F},{"Bksp",13.65F,1.3F,2.1F},
            {"Ins",16.2F,1.3F},{"Home",17.25F,1.3F},{"PgUp",18.3F,1.3F},
            {"NumLk",20,1.3F},{"Num/",21.05F,1.3F},{"Num*",22.1F,1.3F},{"Num-",23.15F,1.3F},
            {"Tab",0,2.35F,1.55F},{"Q",1.6F,2.35F},{"W",2.65F,2.35F},{"E",3.7F,2.35F},
            {"R",4.75F,2.35F},{"T",5.8F,2.35F},{"Y",6.85F,2.35F},{"U",7.9F,2.35F},
            {"I",8.95F,2.35F},{"O",10,2.35F},{"P",11.05F,2.35F},{"[",12.1F,2.35F},
            {"]",13.15F,2.35F},{"\\",14.2F,2.35F,1.55F},{"Del",16.2F,2.35F},{"End",17.25F,2.35F},
            {"PgDn",18.3F,2.35F},{"Num7",20,2.35F},{"Num8",21.05F,2.35F},{"Num9",22.1F,2.35F},
            {"Num+",23.15F,2.35F,1,2.05F},
            {"Caps",0,3.4F,1.85F},{"A",1.9F,3.4F},{"S",2.95F,3.4F},{"D",4,3.4F},{"F",5.05F,3.4F},
            {"G",6.1F,3.4F},{"H",7.15F,3.4F},{"J",8.2F,3.4F},{"K",9.25F,3.4F},{"L",10.3F,3.4F},
            {";",11.35F,3.4F},{"'",12.4F,3.4F},{"Ent",13.45F,3.4F,2.3F},
            {"Num4",20,3.4F},{"Num5",21.05F,3.4F},{"Num6",22.1F,3.4F},
            {"LShift",0,4.45F,2.35F},{"Z",2.4F,4.45F},{"X",3.45F,4.45F},{"C",4.5F,4.45F},
            {"V",5.55F,4.45F},{"B",6.6F,4.45F},{"N",7.65F,4.45F},{"M",8.7F,4.45F},
            {",",9.75F,4.45F},{".",10.8F,4.45F},{"/",11.85F,4.45F},{"RShift",12.9F,4.45F,2.85F},
            {"Up",17.25F,4.45F},{"Num1",20,4.45F},{"Num2",21.05F,4.45F},{"Num3",22.1F,4.45F},
            {"NumEnt",23.15F,4.45F,1,2.05F},
            {"LCtrl",0,5.5F,1.55F},{"LWin",1.6F,5.5F,1.2F},{"LAlt",2.85F,5.5F,1.3F},
            {"Space",4.2F,5.5F,5.8F},{"RAlt",10.05F,5.5F,1.3F},{"RWin",11.4F,5.5F,1.2F,1.0F,"Fn"},
            {"Menu",12.65F,5.5F,1.3F},{"RCtrl",14,5.5F,1.75F},{"Left",16.2F,5.5F},
            {"Down",17.25F,5.5F},{"Right",18.3F,5.5F},{"Num0",20,5.5F,2.1F},{"Num.",22.15F,5.5F}
        };
        const float unit = width / 24.2F;
        const auto bindings = BuildBindingLookup(entries);
        for (const auto& key : keys) DrawVectorKey(draw, origin, unit, key, bindings);
    }

    const std::vector<UHI::HotkeyViewEntry>& DeviceEntries(
        const std::vector<UHI::HotkeyViewGroup>& groups, const std::string_view device)
    {
        static const std::vector<UHI::HotkeyViewEntry> empty;
        const auto found = std::ranges::find_if(groups,
            [&](const UHI::HotkeyViewGroup& group) { return group.device == device; });
        return found == groups.end() ? empty : found->entries;
    }

    const UHI::HotkeyViewEntry* FindVectorBinding(const BindingLookup& bindings, const std::string_view label)
    {
        const auto found = bindings.find(label);
        return found == bindings.end() ? nullptr : found->second;
    }

    void DrawVectorLabel(ImDrawList* draw, const ImVec2 position, const char* label,
        const float size = 10.0F, const ImVec4 color = ImVec4(0.68F, 0.72F, 0.78F, 0.96F))
    {
        ImGui::ImDrawListManager::AddText_FontPtr(draw, ImGui::GetFont(), size, position,
            ImGui::GetColorU32(color), label, label + std::char_traits<char>::length(label), 0.0F, nullptr);
    }

    void DrawVectorBoldLabel(ImDrawList* draw, const ImVec2 position, const char* label,
        const float size, const ImVec4 color)
    {
        DrawVectorLabel(draw, position, label, size, color);
        DrawVectorLabel(draw, ImVec2(position.x + 0.65F * g_uiScale, position.y), label, size, color);
        DrawVectorLabel(draw, ImVec2(position.x + 0.28F * g_uiScale,
            position.y + 0.42F * g_uiScale), label, size, color);
    }

    void DrawVectorRectRegion(ImDrawList* draw, const ImVec2 minimum, const ImVec2 maximum,
        const char* label, const UHI::HotkeyViewEntry* binding, const float rounding,
        const ImDrawFlags flags = 0)
    {
        ImGui::ImDrawListManager::AddRectFilled(draw, minimum, maximum, VectorKeyColor(binding), rounding, flags);
        ImGui::ImDrawListManager::AddRect(draw, minimum, maximum,
            ImGui::GetColorU32(BindingBorderColor(binding, 0.92F)), rounding, flags,
            binding && binding->conflict ? 1.25F : 0.75F);
        if (label[0] != '\0') DrawVectorLabel(draw, ImVec2(minimum.x + 5.0F, minimum.y + 4.0F), label);
        if (ImGui::IsMouseHoveringRect(minimum, maximum, true)) VectorBindingTooltip(binding);
        HandleBindingHotspot(binding, minimum, maximum);
    }

    void DrawVectorCircleRegion(ImDrawList* draw, const ImVec2 center, const float radius,
        const char* label, const UHI::HotkeyViewEntry* binding)
    {
        ImGui::ImDrawListManager::AddCircleFilled(draw, center, radius, VectorKeyColor(binding), 32);
        ImGui::ImDrawListManager::AddCircle(draw, center, radius,
            ImGui::GetColorU32(BindingBorderColor(binding, 0.92F)), 32,
            binding && binding->conflict ? 1.25F : 0.75F);
        const float labelWidth = static_cast<float>(std::char_traits<char>::length(label)) * 5.5F;
        DrawVectorLabel(draw, ImVec2(center.x - labelWidth * 0.5F, center.y - 5.5F), label);
        if (ImGui::IsMouseHoveringRect(ImVec2(center.x - radius, center.y - radius),
                ImVec2(center.x + radius, center.y + radius), true)) VectorBindingTooltip(binding);
        HandleBindingHotspot(binding, ImVec2(center.x - radius, center.y - radius),
            ImVec2(center.x + radius, center.y + radius));
    }

    struct DeviceImageTexture
    {
        Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> view;
        std::uint32_t width{};
        std::uint32_t height{};
        bool attempted{};
    };

    DeviceImageTexture g_mouseImage;
    DeviceImageTexture g_gamepadImage;

    struct ComApartment final
    {
        ComApartment() : result(CoInitializeEx(nullptr, COINIT_MULTITHREADED)) {}
        ~ComApartment()
        {
            if (SUCCEEDED(result)) CoUninitialize();
        }

        [[nodiscard]] bool available() const noexcept
        {
            return SUCCEEDED(result) || result == RPC_E_CHANGED_MODE;
        }

        HRESULT result{};
    };

    bool LoadDeviceImage(DeviceImageTexture& output, const std::filesystem::path& path) noexcept
    {
        if (output.view) return true;

        auto* renderer = RE::BSGraphics::Renderer::GetSingleton();
        auto* device = renderer ? renderer->data.forwarder : nullptr;
        if (!device) return false;
        if (output.attempted) return false;
        output.attempted = true;

        ComApartment apartment;
        if (!apartment.available()) {
            SKSE::log::error("Could not initialize COM while loading UI image: {}", UHI::PathToUtf8(path));
            return false;
        }

        Microsoft::WRL::ComPtr<IWICImagingFactory> factory;
        Microsoft::WRL::ComPtr<IWICBitmapDecoder> decoder;
        Microsoft::WRL::ComPtr<IWICBitmapFrameDecode> frame;
        Microsoft::WRL::ComPtr<IWICFormatConverter> converter;
        HRESULT result = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
            IID_PPV_ARGS(factory.GetAddressOf()));
        if (SUCCEEDED(result)) {
            result = factory->CreateDecoderFromFilename(path.c_str(), nullptr, GENERIC_READ,
                WICDecodeMetadataCacheOnLoad, decoder.GetAddressOf());
        }
        if (SUCCEEDED(result)) result = decoder->GetFrame(0, frame.GetAddressOf());
        if (SUCCEEDED(result)) result = frame->GetSize(&output.width, &output.height);
        if (SUCCEEDED(result)) result = factory->CreateFormatConverter(converter.GetAddressOf());
        if (SUCCEEDED(result)) {
            result = converter->Initialize(frame.Get(), GUID_WICPixelFormat32bppRGBA,
                WICBitmapDitherTypeNone, nullptr, 0.0, WICBitmapPaletteTypeCustom);
        }

        const std::uint64_t stride64 = static_cast<std::uint64_t>(output.width) * 4ULL;
        const std::uint64_t byteCount64 = stride64 * static_cast<std::uint64_t>(output.height);
        if (FAILED(result) || output.width == 0 || output.height == 0 ||
            stride64 > UINT_MAX || byteCount64 > UINT_MAX) {
            SKSE::log::error("Could not decode UI image: {} (HRESULT 0x{:08X})",
                UHI::PathToUtf8(path), static_cast<unsigned>(result));
            return false;
        }

        const auto stride = static_cast<UINT>(stride64);
        const auto byteCount = static_cast<UINT>(byteCount64);
        std::vector<std::uint8_t> pixels(byteCount);
        result = converter->CopyPixels(nullptr, stride, byteCount, pixels.data());
        if (FAILED(result)) {
            SKSE::log::error("Could not read UI image pixels: {} (HRESULT 0x{:08X})",
                UHI::PathToUtf8(path), static_cast<unsigned>(result));
            return false;
        }

        D3D11_TEXTURE2D_DESC description{};
        description.Width = output.width;
        description.Height = output.height;
        description.MipLevels = 1;
        description.ArraySize = 1;
        description.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        description.SampleDesc.Count = 1;
        description.Usage = D3D11_USAGE_DEFAULT;
        description.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        D3D11_SUBRESOURCE_DATA initialData{};
        initialData.pSysMem = pixels.data();
        initialData.SysMemPitch = stride;

        Microsoft::WRL::ComPtr<ID3D11Texture2D> texture;
        result = device->CreateTexture2D(&description, &initialData, texture.GetAddressOf());
        if (SUCCEEDED(result)) {
            result = device->CreateShaderResourceView(texture.Get(), nullptr, output.view.GetAddressOf());
        }
        if (FAILED(result)) {
            SKSE::log::error("Could not create GPU texture for UI image: {} (HRESULT 0x{:08X})",
                UHI::PathToUtf8(path), static_cast<unsigned>(result));
            output.view.Reset();
            return false;
        }

        SKSE::log::info("Loaded UI image: {} ({}x{})", UHI::PathToUtf8(path), output.width, output.height);
        return true;
    }

    std::filesystem::path DeviceImagePath(const wchar_t* fileName)
    {
        return std::filesystem::current_path() / L"Data" / L"SKSE" / L"Plugins" /
            L"UniversalHotkeyManager" / L"assets" / fileName;
    }

    void DrawFallbackVectorMouse(ImDrawList* draw, const ImVec2 origin, const ImVec2 size,
        const std::vector<UHI::HotkeyViewEntry>& entries)
    {
        const auto bindings = BuildBindingLookup(entries);

        const float bodyHeight = size.y * 0.76F;
        const float bodyWidth = (std::min)(size.x * 0.34F, bodyHeight * 0.58F);
        const ImVec2 bodyMin(origin.x + (size.x - bodyWidth) * 0.5F, origin.y + size.y * 0.13F);
        const ImVec2 bodyMax(bodyMin.x + bodyWidth, bodyMin.y + bodyHeight);
        const auto shell = ImGui::GetColorU32(ImVec4(0.105F, 0.125F, 0.155F, 1.0F));
        const auto shellBorder = ImGui::GetColorU32(ImVec4(0.58F, 0.63F, 0.70F, 0.78F));
        ImGui::ImDrawListManager::AddRectFilled(draw, bodyMin, bodyMax, shell, bodyWidth * 0.47F, 0);

        const float buttonBottom = bodyMin.y + bodyHeight * 0.43F;
        const float center = bodyMin.x + bodyWidth * 0.5F;
        DrawVectorRectRegion(draw, ImVec2(bodyMin.x + 3.0F, bodyMin.y + 3.0F),
            ImVec2(center - 2.0F, buttonBottom), "LMB", FindVectorBinding(bindings, "LMB"),
            bodyWidth * 0.43F, ImDrawFlags_RoundCornersTopLeft);
        DrawVectorRectRegion(draw, ImVec2(center + 2.0F, bodyMin.y + 3.0F),
            ImVec2(bodyMax.x - 3.0F, buttonBottom), "RMB", FindVectorBinding(bindings, "RMB"),
            bodyWidth * 0.43F, ImDrawFlags_RoundCornersTopRight);

        const float wheelWidth = (std::max)(12.0F, bodyWidth * 0.13F);
        const ImVec2 wheelMin(center - wheelWidth * 0.5F, bodyMin.y + bodyHeight * 0.12F);
        const ImVec2 wheelMax(center + wheelWidth * 0.5F, bodyMin.y + bodyHeight * 0.31F);
        DrawVectorRectRegion(draw, wheelMin, wheelMax, "", FindVectorBinding(bindings, "MMB"),
            wheelWidth * 0.5F);
        DrawVectorLabel(draw, ImVec2(center - 12.0F, wheelMax.y + 5.0F), "MMB", 9.0F);

        const float sideWidth = (std::max)(16.0F, bodyWidth * 0.18F);
        const float sideHeight = bodyHeight * 0.12F;
        DrawVectorRectRegion(draw, ImVec2(bodyMin.x - sideWidth * 0.55F, bodyMin.y + bodyHeight * 0.48F),
            ImVec2(bodyMin.x + sideWidth * 0.55F, bodyMin.y + bodyHeight * 0.48F + sideHeight),
            "M4", FindVectorBinding(bindings, "M4"), 4.0F);
        DrawVectorRectRegion(draw, ImVec2(bodyMin.x - sideWidth * 0.55F, bodyMin.y + bodyHeight * 0.63F),
            ImVec2(bodyMin.x + sideWidth * 0.55F, bodyMin.y + bodyHeight * 0.63F + sideHeight),
            "M5", FindVectorBinding(bindings, "M5"), 4.0F);
        ImGui::ImDrawListManager::AddRect(draw, bodyMin, bodyMax, shellBorder, bodyWidth * 0.47F, 0, 1.1F);
    }

    void DrawFallbackVectorGamepad(ImDrawList* draw, const ImVec2 origin, const ImVec2 size,
        const std::vector<UHI::HotkeyViewEntry>& entries)
    {
        const auto bindings = BuildBindingLookup(entries);

        const float padWidth = (std::min)(size.x * 0.82F, size.y * 2.25F);
        const float padHeight = (std::min)(size.y * 0.78F, padWidth * 0.47F);
        const ImVec2 pad(origin.x + (size.x - padWidth) * 0.5F, origin.y + size.y * 0.13F);
        const auto shell = ImGui::GetColorU32(ImVec4(0.105F, 0.125F, 0.155F, 1.0F));
        const auto shellBorder = ImGui::GetColorU32(ImVec4(0.58F, 0.63F, 0.70F, 0.78F));

        const ImVec2 leftGrip(pad.x + padWidth * 0.25F, pad.y + padHeight * 0.57F);
        const ImVec2 rightGrip(pad.x + padWidth * 0.75F, pad.y + padHeight * 0.57F);
        const ImVec2 gripRadius(padWidth * 0.18F, padHeight * 0.43F);
        ImGui::ImDrawListManager::AddEllipseFilled(draw, leftGrip, gripRadius, shell, -0.30F, 36);
        ImGui::ImDrawListManager::AddEllipseFilled(draw, rightGrip, gripRadius, shell, 0.30F, 36);
        ImGui::ImDrawListManager::AddRectFilled(draw,
            ImVec2(pad.x + padWidth * 0.18F, pad.y + padHeight * 0.16F),
            ImVec2(pad.x + padWidth * 0.82F, pad.y + padHeight * 0.69F), shell, padHeight * 0.23F, 0);
        ImGui::ImDrawListManager::AddEllipse(draw, leftGrip, gripRadius, shellBorder, -0.30F, 36, 1.0F);
        ImGui::ImDrawListManager::AddEllipse(draw, rightGrip, gripRadius, shellBorder, 0.30F, 36, 1.0F);

        const float shoulderY = pad.y + padHeight * 0.03F;
        const float shoulderW = padWidth * 0.22F;
        const float shoulderH = padHeight * 0.15F;
        DrawVectorRectRegion(draw, ImVec2(pad.x + padWidth * 0.13F, shoulderY),
            ImVec2(pad.x + padWidth * 0.13F + shoulderW, shoulderY + shoulderH),
            "LB", FindVectorBinding(bindings, "LB"), 6.0F);
        DrawVectorRectRegion(draw, ImVec2(pad.x + padWidth * 0.65F, shoulderY),
            ImVec2(pad.x + padWidth * 0.65F + shoulderW, shoulderY + shoulderH),
            "RB", FindVectorBinding(bindings, "RB"), 6.0F);
        DrawVectorRectRegion(draw, ImVec2(pad.x + padWidth * 0.16F, shoulderY + shoulderH + 3.0F),
            ImVec2(pad.x + padWidth * 0.16F + shoulderW * 0.72F, shoulderY + shoulderH * 1.74F),
            "LT", FindVectorBinding(bindings, "LT"), 5.0F);
        DrawVectorRectRegion(draw, ImVec2(pad.x + padWidth * 0.68F, shoulderY + shoulderH + 3.0F),
            ImVec2(pad.x + padWidth * 0.68F + shoulderW * 0.72F, shoulderY + shoulderH * 1.74F),
            "RT", FindVectorBinding(bindings, "RT"), 5.0F);

        const ImVec2 dpad(pad.x + padWidth * 0.26F, pad.y + padHeight * 0.43F);
        const float d = (std::max)(10.0F, padHeight * 0.105F);
        DrawVectorRectRegion(draw, ImVec2(dpad.x - d * 0.5F, dpad.y - d * 1.48F),
            ImVec2(dpad.x + d * 0.5F, dpad.y - d * 0.40F), "", FindVectorBinding(bindings, "DUp"), 2.0F);
        DrawVectorRectRegion(draw, ImVec2(dpad.x - d * 1.48F, dpad.y - d * 0.5F),
            ImVec2(dpad.x - d * 0.40F, dpad.y + d * 0.5F), "", FindVectorBinding(bindings, "DLeft"), 2.0F);
        DrawVectorRectRegion(draw, ImVec2(dpad.x + d * 0.40F, dpad.y - d * 0.5F),
            ImVec2(dpad.x + d * 1.48F, dpad.y + d * 0.5F), "", FindVectorBinding(bindings, "DRight"), 2.0F);
        DrawVectorRectRegion(draw, ImVec2(dpad.x - d * 0.5F, dpad.y + d * 0.40F),
            ImVec2(dpad.x + d * 0.5F, dpad.y + d * 1.48F), "", FindVectorBinding(bindings, "DDown"), 2.0F);

        const ImVec2 face(pad.x + padWidth * 0.75F, pad.y + padHeight * 0.42F);
        const float faceR = (std::max)(9.0F, padHeight * 0.085F);
        DrawVectorCircleRegion(draw, ImVec2(face.x, face.y - faceR * 1.75F), faceR,
            "Y", FindVectorBinding(bindings, "Y"));
        DrawVectorCircleRegion(draw, ImVec2(face.x - faceR * 1.75F, face.y), faceR,
            "X", FindVectorBinding(bindings, "X"));
        DrawVectorCircleRegion(draw, ImVec2(face.x + faceR * 1.75F, face.y), faceR,
            "B", FindVectorBinding(bindings, "B"));
        DrawVectorCircleRegion(draw, ImVec2(face.x, face.y + faceR * 1.75F), faceR,
            "A", FindVectorBinding(bindings, "A"));

        const float stickR = (std::max)(12.0F, padHeight * 0.115F);
        DrawVectorCircleRegion(draw, ImVec2(pad.x + padWidth * 0.39F, pad.y + padHeight * 0.70F), stickR,
            "LS", FindVectorBinding(bindings, "Left Stick"));
        DrawVectorCircleRegion(draw, ImVec2(pad.x + padWidth * 0.39F, pad.y + padHeight * 0.70F), stickR * 0.52F,
            "CLICK", FindVectorBinding(bindings, "Left Stick Click"));
        DrawVectorCircleRegion(draw, ImVec2(pad.x + padWidth * 0.61F, pad.y + padHeight * 0.70F), stickR,
            "RS", FindVectorBinding(bindings, "Right Stick"));
        DrawVectorCircleRegion(draw, ImVec2(pad.x + padWidth * 0.61F, pad.y + padHeight * 0.70F), stickR * 0.52F,
            "CLICK", FindVectorBinding(bindings, "Right Stick Click"));
        DrawVectorRectRegion(draw, ImVec2(pad.x + padWidth * 0.42F, pad.y + padHeight * 0.39F),
            ImVec2(pad.x + padWidth * 0.48F, pad.y + padHeight * 0.48F),
            "", FindVectorBinding(bindings, "Back"), 6.0F);
        DrawVectorRectRegion(draw, ImVec2(pad.x + padWidth * 0.52F, pad.y + padHeight * 0.39F),
            ImVec2(pad.x + padWidth * 0.58F, pad.y + padHeight * 0.48F),
            "", FindVectorBinding(bindings, "Start"), 6.0F);
        DrawVectorLabel(draw, ImVec2(pad.x + padWidth * 0.415F, pad.y + padHeight * 0.49F), "BACK", 8.0F);
        DrawVectorLabel(draw, ImVec2(pad.x + padWidth * 0.515F, pad.y + padHeight * 0.49F), "START", 8.0F);
    }

    struct DeviceImagePlacement
    {
        ImVec2 minimum;
        ImVec2 maximum;
        ImVec2 extent;
    };

    DeviceImagePlacement DrawDeviceImage(ImDrawList* draw, const ImVec2 origin, const ImVec2 size,
        const DeviceImageTexture& image)
    {
        const float contentTop = origin.y + 20.0F;
        const float maximumWidth = (std::max)(1.0F, size.x - 4.0F);
        const float maximumHeight = (std::max)(1.0F, size.y - 24.0F);
        const float scale = (std::min)(maximumWidth / static_cast<float>(image.width),
            maximumHeight / static_cast<float>(image.height));
        const ImVec2 extent(static_cast<float>(image.width) * scale,
            static_cast<float>(image.height) * scale);
        const ImVec2 minimum(origin.x + (size.x - extent.x) * 0.5F,
            contentTop + (maximumHeight - extent.y) * 0.5F);
        const ImVec2 maximum(minimum.x + extent.x, minimum.y + extent.y);
        ImGui::ImDrawListManager::AddImage(draw, reinterpret_cast<ImTextureID>(image.view.Get()),
            minimum, maximum, ImVec2(0.0F, 0.0F), ImVec2(1.0F, 1.0F),
            ImGui::GetColorU32(ImVec4(0.50F, 0.52F, 0.56F, 0.58F)));
        return { minimum, maximum, extent };
    }

    ImVec2 DeviceImagePoint(const DeviceImagePlacement& image, const float x, const float y)
    {
        return ImVec2(image.minimum.x + image.extent.x * x,
            image.minimum.y + image.extent.y * y);
    }

    ImVec4 DeviceHotspotFillColor(const UHI::HotkeyViewEntry* binding, const float alpha)
    {
        if (!binding) return ImVec4(0.55F, 0.59F, 0.64F, alpha * 0.65F);
        return CategoryKeyColor(binding->category, alpha);
    }

    ImVec4 DeviceHotspotBorderColor(const UHI::HotkeyViewEntry* binding, const float alpha)
    {
        return BindingBorderColor(binding, alpha);
    }

    float MeasureVectorText(const std::string_view text, const float fontSize)
    {
        if (text.empty()) return 0.0F;
        ImVec2 measured{};
        ImGui::ImFontManger::CalcTextSizeA(&measured, ImGui::GetFont(), fontSize,
            (std::numeric_limits<float>::max)(), 0.0F, text.data(), text.data() + text.size(), nullptr);
        return measured.x;
    }

    void DrawDeviceHotspotLabel(ImDrawList* draw, const ImVec2 center, const char* label,
        const UHI::HotkeyViewEntry* binding, const float maximumWidth)
    {
        const float fontSize = (std::max)(12.5F, 12.25F * g_uiScale);
        const auto visibleLabel = EllipsizeVectorText(LocalizedDeviceButtonName(label, label),
            fontSize, maximumWidth);
        const float labelWidth = MeasureVectorText(visibleLabel, fontSize);
        const ImVec2 textPosition(center.x - labelWidth * 0.5F, center.y - fontSize * 0.5F);
        const ImVec2 pillMinimum(textPosition.x - 2.5F, textPosition.y - 1.5F);
        const ImVec2 pillMaximum(textPosition.x + labelWidth + 2.5F, textPosition.y + fontSize + 1.5F);
        ImGui::ImDrawListManager::AddRectFilled(draw, pillMinimum, pillMaximum,
            ImGui::GetColorU32(ImVec4(0.02F, 0.025F, 0.035F, 0.78F)), 2.5F, 0);
        DrawVectorBoldLabel(draw, textPosition, visibleLabel.c_str(), fontSize,
            DeviceHotspotBorderColor(binding, binding ? 0.98F : 0.78F));
    }

    struct DeviceCallout
    {
        const char* physicalKey;
        const char* label;
        float anchorX;
        float anchorY;
        bool left;
    };

    void DrawDeviceCallouts(ImDrawList* draw, const ImVec2 panelOrigin, const ImVec2 panelSize,
        const DeviceImagePlacement& image, const BindingLookup& bindings,
        const std::span<const DeviceCallout> callouts, const float keyNameSize,
        const float ownerSize, const float actionSize)
    {
        const auto leftCount = static_cast<std::size_t>(std::ranges::count_if(callouts,
            [&](const DeviceCallout& value) {
                return value.left && FindVectorBinding(bindings, value.physicalKey) != nullptr;
            }));
        const auto rightCount = static_cast<std::size_t>(std::ranges::count_if(callouts,
            [&](const DeviceCallout& value) {
                return !value.left && FindVectorBinding(bindings, value.physicalKey) != nullptr;
            }));
        std::size_t leftIndex{};
        std::size_t rightIndex{};
        const float outerMargin = 10.0F;
        const float innerGap = 12.0F;
        const float leftWidth = (std::max)(110.0F, image.minimum.x - panelOrigin.x - innerGap - outerMargin);
        const float rightWidth = (std::max)(110.0F,
            panelOrigin.x + panelSize.x - image.maximum.x - innerGap - outerMargin);
        const float usableHeight = (std::max)(80.0F, panelSize.y - 38.0F);
        for (const auto& callout : callouts) {
            const auto* binding = FindVectorBinding(bindings, callout.physicalKey);
            if (!binding || !binding->record) continue;
            const auto count = (std::max<std::size_t>)(1U, callout.left ? leftCount : rightCount);
            const auto index = callout.left ? leftIndex++ : rightIndex++;
            const float slotHeight = usableHeight / static_cast<float>(count);
            const float boxHeight = std::clamp(slotHeight - 3.0F,
                58.0F * g_uiScale, 94.0F * g_uiScale);
            const float centerY = panelOrigin.y + 26.0F + slotHeight * (static_cast<float>(index) + 0.5F);
            const float boxWidth = callout.left ? leftWidth : rightWidth;
            const ImVec2 minimum(callout.left ? panelOrigin.x + outerMargin :
                panelOrigin.x + panelSize.x - outerMargin - boxWidth, centerY - boxHeight * 0.5F);
            const ImVec2 maximum(minimum.x + boxWidth, minimum.y + boxHeight);
            const auto color = CategoryTextColor(binding->category);
            ImGui::ImDrawListManager::AddRectFilled(draw, minimum, maximum,
                ImGui::GetColorU32(ImVec4(0.0F, 0.0F, 0.0F, 1.0F)), 3.0F, 0);
            ImGui::ImDrawListManager::AddRect(draw, minimum, maximum,
                ImGui::GetColorU32(ImVec4(color.x, color.y, color.z, 0.72F)), 3.0F, 0, 0.75F);
            const ImVec2 anchor = DeviceImagePoint(image, callout.anchorX, callout.anchorY);
            const ImVec2 edge(callout.left ? maximum.x : minimum.x, centerY);
            const float elbowX = callout.left ? anchor.x - innerGap : anchor.x + innerGap;
            ImGui::ImDrawListManager::AddLine(draw, anchor, ImVec2(elbowX, anchor.y),
                ImGui::GetColorU32(ImVec4(color.x, color.y, color.z, 0.78F)), 1.0F);
            ImGui::ImDrawListManager::AddLine(draw, ImVec2(elbowX, anchor.y), edge,
                ImGui::GetColorU32(ImVec4(color.x, color.y, color.z, 0.78F)), 1.0F);
            // These sizes are supplied by RenderVectorOverview from the same
            // keyboard unit used by DrawVectorKey.  Fixed pixel sizes looked
            // progressively smaller than keyboard labels on large windows.
            const float horizontalPadding = 9.0F;
            const float textWidth = boxWidth - horizontalPadding * 2.0F;
            const char* localizedKey = LocalizedDeviceButtonName(callout.physicalKey, callout.label);
            const float keyLimit = (std::max)(34.0F * g_uiScale, textWidth * 0.42F);
            const auto visibleKey = EllipsizeVectorText(localizedKey, keyNameSize, keyLimit);
            const float keyWidth = MeasureVectorText(visibleKey, keyNameSize);
            // Key and owner share the first line exactly like the keyboard:
            // the physical key is left-aligned in neutral grey and the mod
            // owner is right-aligned in the category colour.
            const float ownerWidth = (std::max)(18.0F,
                textWidth - keyWidth - 10.0F * g_uiScale);
            const auto owner = EllipsizeVectorText(binding->record->owner, ownerSize, ownerWidth);
            const auto action = WrapVectorTextTwoLines(UHI::CompactActionLabel(
                DisplayAction(*binding->record)), actionSize, textWidth);
            DrawVectorBoldLabel(draw, ImVec2(minimum.x + horizontalPadding, minimum.y + 4.0F),
                visibleKey.c_str(), keyNameSize, ImVec4(0.56F, 0.58F, 0.62F, 0.98F));
            const float ownerTextWidth = MeasureVectorText(owner, ownerSize);
            DrawVectorBoldLabel(draw,
                ImVec2(maximum.x - ownerTextWidth - horizontalPadding, minimum.y + 4.0F), owner.c_str(),
                ownerSize, color);
            const float actionStartY = action[1].empty() ?
                minimum.y + boxHeight - actionSize - 5.0F :
                minimum.y + boxHeight - actionSize * 2.0F - 7.0F;
            DrawVectorBoldLabel(draw, ImVec2(minimum.x + horizontalPadding, actionStartY),
                action[0].c_str(), actionSize, ImVec4(0.96F, 0.97F, 1.0F, 1.0F));
            if (!action[1].empty()) {
                DrawVectorBoldLabel(draw, ImVec2(minimum.x + horizontalPadding,
                    actionStartY + actionSize + 2.0F), action[1].c_str(), actionSize,
                    ImVec4(0.96F, 0.97F, 1.0F, 1.0F));
            }
            if (ImGui::IsMouseHoveringRect(minimum, maximum, true)) VectorBindingTooltip(binding);
            HandleBindingHotspot(binding, minimum, maximum);
        }
    }

    void DrawDeviceRectHotspot(ImDrawList* draw, const DeviceImagePlacement& image,
        const float x1, const float y1, const float x2, const float y2, const char* label,
        const UHI::HotkeyViewEntry* binding, const float rounding = 4.0F)
    {
        (void)draw;
        (void)label;
        (void)rounding;
        const ImVec2 minimum = DeviceImagePoint(image, x1, y1);
        const ImVec2 maximum = DeviceImagePoint(image, x2, y2);
        // Keep the illustration clean while retaining this invisible hit
        // target for hover, rename, and binding-edit interactions.
        if (ImGui::IsMouseHoveringRect(minimum, maximum, true)) VectorBindingTooltip(binding);
        HandleBindingHotspot(binding, minimum, maximum);
    }

    void DrawDeviceCircleHotspot(ImDrawList* draw, const DeviceImagePlacement& image,
        const float x, const float y, const float radius, const char* label,
        const UHI::HotkeyViewEntry* binding)
    {
        (void)draw;
        (void)label;
        const ImVec2 center = DeviceImagePoint(image, x, y);
        const float scaledRadius = image.extent.y * radius;
        if (ImGui::IsMouseHoveringRect(ImVec2(center.x - scaledRadius, center.y - scaledRadius),
                ImVec2(center.x + scaledRadius, center.y + scaledRadius), true)) {
            VectorBindingTooltip(binding);
        }
        HandleBindingHotspot(binding, ImVec2(center.x - scaledRadius, center.y - scaledRadius),
            ImVec2(center.x + scaledRadius, center.y + scaledRadius));
    }

    void DrawVectorMouse(ImDrawList* draw, const ImVec2 origin, const ImVec2 size,
        const std::vector<UHI::HotkeyViewEntry>& entries, const float keyNameSize,
        const float ownerSize, const float actionSize)
    {
        if (!LoadDeviceImage(g_mouseImage, DeviceImagePath(L"mouse.png"))) {
            DrawFallbackVectorMouse(draw, origin, size, entries);
            return;
        }

        const float calloutMargin = size.x * 0.32F;
        const auto image = DrawDeviceImage(draw, ImVec2(origin.x + calloutMargin, origin.y),
            ImVec2(size.x - calloutMargin * 2.0F, size.y), g_mouseImage);
        const auto bindings = BuildBindingLookup(entries);
        DrawDeviceRectHotspot(draw, image, 0.17F, 0.05F, 0.47F, 0.51F,
            "LMB", FindVectorBinding(bindings, "LMB"), 8.0F);
        DrawDeviceRectHotspot(draw, image, 0.53F, 0.05F, 0.84F, 0.51F,
            "RMB", FindVectorBinding(bindings, "RMB"), 8.0F);
        DrawDeviceRectHotspot(draw, image, 0.455F, 0.135F, 0.545F, 0.32F,
            "MMB", FindVectorBinding(bindings, "MMB"), 5.0F);
        DrawDeviceRectHotspot(draw, image, 0.145F, 0.335F, 0.225F, 0.465F,
            "M5", FindVectorBinding(bindings, "M5"), 4.0F);
        DrawDeviceRectHotspot(draw, image, 0.155F, 0.475F, 0.235F, 0.59F,
            "M4", FindVectorBinding(bindings, "M4"), 4.0F);
        static constexpr DeviceCallout callouts[]{
            { "LMB", "Left Click", 0.30F, 0.24F, true },
            { "M5", "Side BT UP", 0.18F, 0.40F, true },
            { "M4", "Side BT DN", 0.19F, 0.53F, true },
            { "RMB", "Right Click", 0.69F, 0.24F, false },
            { "MMB", "Middle Click", 0.50F, 0.22F, false }
        };
        DrawDeviceCallouts(draw, origin, size, image, bindings, callouts,
            keyNameSize, ownerSize, actionSize);
    }

    void DrawVectorGamepad(ImDrawList* draw, const ImVec2 origin, const ImVec2 size,
        const std::vector<UHI::HotkeyViewEntry>& entries, const float keyNameSize,
        const float ownerSize, const float actionSize)
    {
        if (!LoadDeviceImage(g_gamepadImage, DeviceImagePath(L"gamepad.png"))) {
            DrawFallbackVectorGamepad(draw, origin, size, entries);
            return;
        }

        const float calloutMargin = size.x * 0.245F;
        const auto image = DrawDeviceImage(draw, ImVec2(origin.x + calloutMargin, origin.y),
            ImVec2(size.x - calloutMargin * 2.0F, size.y), g_gamepadImage);
        const auto bindings = BuildBindingLookup(entries);

        DrawDeviceRectHotspot(draw, image, 0.17F, 0.045F, 0.34F, 0.13F,
            "LB", FindVectorBinding(bindings, "LB"), 5.0F);
        DrawDeviceRectHotspot(draw, image, 0.66F, 0.045F, 0.83F, 0.13F,
            "RB", FindVectorBinding(bindings, "RB"), 5.0F);
        DrawDeviceRectHotspot(draw, image, 0.20F, 0.012F, 0.31F, 0.075F,
            "LT", FindVectorBinding(bindings, "LT"), 4.0F);
        DrawDeviceRectHotspot(draw, image, 0.69F, 0.012F, 0.80F, 0.075F,
            "RT", FindVectorBinding(bindings, "RT"), 4.0F);

        DrawDeviceCircleHotspot(draw, image, 0.242F, 0.317F, 0.098F,
            "LS", FindVectorBinding(bindings, "Left Stick"));
        DrawDeviceCircleHotspot(draw, image, 0.242F, 0.317F, 0.050F,
            "LS Click", FindVectorBinding(bindings, "Left Stick Click"));
        DrawDeviceCircleHotspot(draw, image, 0.636F, 0.552F, 0.090F,
            "RS", FindVectorBinding(bindings, "Right Stick"));
        DrawDeviceCircleHotspot(draw, image, 0.636F, 0.552F, 0.046F,
            "RS Click", FindVectorBinding(bindings, "Right Stick Click"));
        DrawDeviceCircleHotspot(draw, image, 0.430F, 0.347F, 0.035F,
            "Back", FindVectorBinding(bindings, "Back"));
        DrawDeviceCircleHotspot(draw, image, 0.570F, 0.347F, 0.035F,
            "Start", FindVectorBinding(bindings, "Start"));

        DrawDeviceRectHotspot(draw, image, 0.340F, 0.425F, 0.385F, 0.505F,
            "DU", FindVectorBinding(bindings, "DUp"), 2.0F);
        DrawDeviceRectHotspot(draw, image, 0.340F, 0.535F, 0.385F, 0.625F,
            "DD", FindVectorBinding(bindings, "DDown"), 2.0F);
        DrawDeviceRectHotspot(draw, image, 0.295F, 0.495F, 0.350F, 0.555F,
            "DL", FindVectorBinding(bindings, "DLeft"), 2.0F);
        DrawDeviceRectHotspot(draw, image, 0.375F, 0.495F, 0.435F, 0.555F,
            "DR", FindVectorBinding(bindings, "DRight"), 2.0F);

        DrawDeviceCircleHotspot(draw, image, 0.762F, 0.213F, 0.052F,
            "Y", FindVectorBinding(bindings, "Y"));
        DrawDeviceCircleHotspot(draw, image, 0.690F, 0.327F, 0.052F,
            "X", FindVectorBinding(bindings, "X"));
        DrawDeviceCircleHotspot(draw, image, 0.837F, 0.327F, 0.052F,
            "B", FindVectorBinding(bindings, "B"));
        DrawDeviceCircleHotspot(draw, image, 0.762F, 0.449F, 0.052F,
            "A", FindVectorBinding(bindings, "A"));
        static constexpr DeviceCallout callouts[]{
            { "LT", "LT", 0.255F, 0.043F, true },
            { "LB", "LB", 0.255F, 0.090F, true },
            { "Left Stick Click", "LS Click", 0.242F, 0.317F, true },
            { "DUp", "D-Pad Up", 0.363F, 0.465F, true },
            { "DLeft", "D-Pad Left", 0.323F, 0.525F, true },
            { "DDown", "D-Pad Down", 0.363F, 0.580F, true },
            { "DRight", "D-Pad Right", 0.405F, 0.525F, true },
            { "Back", "Back", 0.430F, 0.347F, true },
            { "RT", "RT", 0.745F, 0.043F, false },
            { "RB", "RB", 0.745F, 0.090F, false },
            { "Right Stick Click", "RS Click", 0.636F, 0.552F, false },
            { "Y", "Y", 0.762F, 0.213F, false },
            { "X", "X", 0.690F, 0.327F, false },
            { "B", "B", 0.837F, 0.327F, false },
            { "A", "A", 0.762F, 0.449F, false },
            { "Start", "Start", 0.570F, 0.347F, false }
        };
        DrawDeviceCallouts(draw, origin, size, image, bindings, callouts,
            keyNameSize, ownerSize, actionSize);
    }

    void RenderVectorOverview(const std::vector<UHI::HotkeyViewGroup>& groups)
    {
        ImVec2 available{};
        ImGui::GetContentRegionAvail(&available);
        ImVec2 origin{};
        ImGui::GetCursorScreenPos(&origin);
        const float width = (std::max)(320.0F, available.x - 16.0F);
        const float keyboardUnit = width / 24.2F;
        const float keyboardHeight = keyboardUnit * 6.55F + 24.0F;
        const float deviceKeyNameSize = VectorKeyNameSize(keyboardUnit);
        const float deviceOwnerSize = VectorOwnerSize(keyboardUnit);
        const float deviceActionSize = VectorActionSize(keyboardUnit);
        // The mouse and gamepad are deliberately given more vertical room than
        // the original fixed strip.  They can now grow with the window instead
        // of remaining thumbnail-sized underneath the keyboard.
        const float lowerHeight = std::clamp(
            (std::max)(available.y - keyboardHeight - 12.0F, width * 0.27F), 500.0F, 650.0F);
        auto* draw = ImGui::GetWindowDrawList();
        DrawVectorKeyboard(draw, ImVec2(origin.x + 8.0F, origin.y + 8.0F), width - 16.0F,
            DeviceEntries(groups, "keyboard"));
        const float gap = 14.0F;
        // Mouse callouts consume 32% of its card on each side, while gamepad
        // callouts consume 24.5%. A 43/57 card split therefore gives both
        // devices nearly equal real callout widths.
        const float mouseWidth = (width - gap) * 0.43F;
        const ImVec2 lowerOrigin(origin.x, origin.y + keyboardHeight);
        DrawVectorMouse(draw, lowerOrigin, ImVec2(mouseWidth, lowerHeight), DeviceEntries(groups, "mouse"),
            deviceKeyNameSize, deviceOwnerSize, deviceActionSize);
        DrawVectorGamepad(draw, ImVec2(lowerOrigin.x + mouseWidth + gap, lowerOrigin.y),
            ImVec2(width - mouseWidth - gap, lowerHeight), DeviceEntries(groups, "gamepad"),
            deviceKeyNameSize, deviceOwnerSize, deviceActionSize);
        ImGui::Dummy(ImVec2(width, keyboardHeight + lowerHeight));
    }

    void RenderDeviceBoard(const UHI::HotkeyViewGroup& group)
    {
        const char* deviceLabel = group.device == "keyboard" ? UiText("keyboard", "키보드", "键盘") :
            group.device == "mouse" ? UiText("mouse", "마우스", "鼠标") :
            group.device == "gamepad" ? UiText("gamepad", "게임패드", "手柄") : group.device.c_str();
        ImGui::TextDisabled("%s", deviceLabel);
        if (group.device == "keyboard") {
            RenderKeyboardBoard(group.entries);
            return;
        }
        if (group.device == "mouse") {
            RenderMouseBoard(group.entries);
            return;
        }
        if (group.device == "gamepad") {
            RenderGamepadBoard(group.entries);
            return;
        }
        const int columns = group.device == "keyboard" ? 8 : 4;
        if (ImGui::BeginTable(("##" + group.device).c_str(), columns,
            ImGuiTableFlags_SizingStretchSame)) {
            for (const auto& item : group.entries) {
                const auto& record = *item.record;
                ImGui::TableNextColumn();
                const ImVec4 color = CategoryKeyColor(item.category);
                ImGui::PushStyleColor(ImGuiCol_Button, color);
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(color.x + 0.10F,
                    color.y + 0.10F, color.z + 0.10F, 1.0F));
                ImGui::PushStyleColor(ImGuiCol_Border, BindingBorderColor(&item));
                ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize,
                    item.conflict ? 1.25F : 0.65F);
                const auto friendlyBinding = FriendlyBindingLabel(item.displayBinding);
                ImGui::Button(friendlyBinding.c_str(), ImVec2(-1.0F,
                    (std::max)(32.0F * g_uiScale, ImGui::GetFrameHeight())));
                ImGui::PopStyleVar();
                ImGui::PopStyleColor(3);
                if (ImGui::IsItemHovered()) {
                    ImGui::BeginTooltip();
                    const auto action = DisplayAction(record);
                    ImGui::TextUnformatted(action.c_str());
                    ImGui::Text("%s: %s", UiText("Owner", "모드", "所属模组"), record.owner.c_str());
                    ImGui::Text("%s: %s", UiText("Category", "카테고리", "类别"), CategoryLabel(item.category));
                    ImGui::Text("%s: %s (%s)", UiText("Active in", "활성 컨텍스트", "生效上下文"),
                        LocalizedContextLabel(record.contextMask).c_str(),
                        ContextConfidenceName(record.contextConfidence));
                    ImGui::Text("%s: %s%s", UiText("Status", "상태", "状态"),
                        record.editable ? UiText("editable", "변경 가능", "可修改") : UiText("read-only", "읽기 전용", "只读"),
                        item.conflict ? UiText(" | CONFIRMED CONFLICT", " | 확정 충돌", " | 已确认冲突") :
                        item.conditionalConflict ? UiText(" | OVERLAP", " | 중복", " | 重叠") : "");
                    const auto evidence = UHI::PathToUtf8(record.evidencePath);
                    ImGui::Text("%s: %s:%zu", UiText("Source", "출처", "来源"),
                        evidence.c_str(), record.evidenceLine);
                    ImGui::EndTooltip();
                }
            }
            ImGui::EndTable();
        }
    }

    const char* ConfidenceName(const UHI::Confidence confidence)
    {
        switch (confidence) {
        case UHI::Confidence::confirmed:
            return UiText("confirmed", "확정", "已确认");
        case UHI::Confidence::inferred:
            return UiText("inferred", "추정", "推断");
        default:
            return UiText("candidate", "후보", "候选");
        }
    }

    const char* ContextConfidenceName(const UHI::ContextConfidence confidence)
    {
        switch (confidence) {
        case UHI::ContextConfidence::confirmed:
            return UiText("confirmed", "확정", "已确认");
        case UHI::ContextConfidence::inferred:
            return UiText("inferred", "추정", "推断");
        default:
            return UiText("unknown", "불명", "未知");
        }
    }

    std::string LocalizedContextLabel(const std::uint32_t mask, const bool compact)
    {
        const auto has = [&](const UHI::ActivationContext context) {
            return (mask & static_cast<std::uint32_t>(context)) != 0;
        };
        if (mask == 0) return UiText("Unknown", "불명", "未知");
        if (has(UHI::ActivationContext::global)) return UiText("Global", "전역", "全局");
        const std::array contexts{
            std::pair{ UHI::ActivationContext::gameplay, UiText("Field", "필드", "游戏场景") },
            std::pair{ UHI::ActivationContext::menuMode, UiText("Menu common", "메뉴 공통", "菜单通用") },
            std::pair{ UHI::ActivationContext::console, UiText("Console", "콘솔", "控制台") },
            std::pair{ UHI::ActivationContext::itemMenu, UiText("Item menus", "아이템 메뉴", "物品菜单") },
            std::pair{ UHI::ActivationContext::inventory, UiText("Inventory", "인벤", "物品栏") },
            std::pair{ UHI::ActivationContext::favorites, UiText("Favorites", "즐겨찾기", "收藏") },
            std::pair{ UHI::ActivationContext::map, UiText("Map", "지도", "地图") },
            std::pair{ UHI::ActivationContext::stats, UiText("Skills", "능력", "技能") },
            std::pair{ UHI::ActivationContext::cursor, UiText("Cursor", "커서", "光标") },
            std::pair{ UHI::ActivationContext::book, UiText("Book", "책", "书籍") },
            std::pair{ UHI::ActivationContext::journal, UiText("System / MCM", "시스템/MCM", "系统/MCM") },
            std::pair{ UHI::ActivationContext::lockpicking, UiText("Lockpick", "락픽", "开锁") },
            std::pair{ UHI::ActivationContext::externalOverlay, UiText("External overlay", "외부 오버레이", "外部叠加层") },
            std::pair{ UHI::ActivationContext::debugText, UiText("Debug text", "디버그 텍스트", "调试文本") },
            std::pair{ UHI::ActivationContext::debugOverlay, UiText("Debug overlay", "디버그 오버레이", "调试叠加层") },
            std::pair{ UHI::ActivationContext::tfcMode, UiText("Free camera", "자유 카메라", "自由镜头") },
            std::pair{ UHI::ActivationContext::mapDebug, UiText("Debug map", "디버그 지도", "调试地图") },
            std::pair{ UHI::ActivationContext::marketplace, UiText("Creations menu", "크리에이션 메뉴", "创作菜单") },
            std::pair{ UHI::ActivationContext::favor, UiText("Favor", "대화 의뢰", "交互") },
            std::pair{ UHI::ActivationContext::sexLabScene, UiText("SexLab scene", "SexLab 장면", "SexLab 场景") }
        };
        std::vector<const char*> labels;
        for (const auto& [context, label] : contexts) if (has(context)) labels.push_back(label);
        if (labels.empty()) return UiText("Unknown", "불명", "未知");
        if (compact && labels.size() > 1) {
            return std::string(labels.front()) + UiText(" +", " 외 ", " +") +
                std::to_string(labels.size() - 1);
        }
        std::string result;
        for (const auto* label : labels) {
            if (!result.empty()) result += ", ";
            result += label;
        }
        return result;
    }

    void UpdateFilteredGroups(const std::shared_ptr<const RegistrySnapshot>& snapshot,
        const UHI::HotkeyCategory category)
    {
        constexpr std::array deviceKeys{ "", "keyboard", "mouse", "gamepad" };
        const std::string_view device = deviceKeys[std::clamp(g_device, 0, 3)];
        const auto search = UHI::NormalizeBinding(g_search);
        if (g_filteredSnapshot == snapshot && g_filteredCategory == category &&
            g_filteredDevice == g_device && g_filteredConflictsOnly == g_conflictsOnly &&
            g_filteredOverlapsOnly == g_overlapsOnly &&
            g_filteredConflictsFirst == g_conflictsFirst && g_filteredSearch == search) return;

        g_filteredSnapshot = snapshot;
        g_filteredCategory = category;
        g_filteredDevice = g_device;
        g_filteredConflictsOnly = g_conflictsOnly;
        g_filteredOverlapsOnly = g_overlapsOnly;
        g_filteredConflictsFirst = g_conflictsFirst;
        g_filteredSearch = search;
        static const std::vector<UHI::HotkeyViewGroup> emptyGroups;
        g_filteredGroups = UHI::FilterHotkeyView(snapshot ? snapshot->groups : emptyGroups,
            category, device, false);
        for (auto& group : g_filteredGroups) {
            std::erase_if(group.entries, [&](const UHI::HotkeyViewEntry& item) {
                const auto& record = *item.record;
                if ((g_conflictsOnly || g_overlapsOnly) &&
                    !((g_conflictsOnly && item.conflict) ||
                        (g_overlapsOnly && item.conditionalConflict))) return true;
                if (search.empty()) return false;
                return UHI::NormalizeBinding(item.displayBinding).find(search) == std::string::npos &&
                    UHI::NormalizeBinding(DisplayAction(record)).find(search) == std::string::npos &&
                    UHI::NormalizeBinding(record.owner).find(search) == std::string::npos;
            });
        }
        std::erase_if(g_filteredGroups,
            [](const UHI::HotkeyViewGroup& group) { return group.entries.empty(); });
        if (g_conflictsFirst) {
            for (auto& group : g_filteredGroups) {
                std::stable_sort(group.entries.begin(), group.entries.end(),
                    [](const auto& left, const auto& right) {
                        const auto rank = [](const auto& item) {
                            return item.conflict ? 2 : item.conditionalConflict ? 1 : 0;
                        };
                        return rank(left) > rank(right);
                    });
            }
        }
    }

    void RenderFilterToolbar()
    {
        const char* deviceItems[] = {
            UiText("All devices", "모든 장치", "所有设备"),
            UiText("keyboard", "키보드", "键盘"),
            UiText("mouse", "마우스", "鼠标"),
            UiText("gamepad", "게임패드", "手柄")
        };
        const char* deviceView = UiText("Device view", "장치 보기", "设备视图");
        const char* bindingView = UiText("Hotkey manager", "단축키 관리", "热键管理");
        const char* conflictsOnly = UiText("Conflicts", "충돌", "冲突");
        const char* overlapsOnly = UiText("Overlaps", "중복", "重叠");
        const auto deviceButtonSize = ScaledButtonSize(deviceView, 112.0F, 32.0F);
        const auto bindingButtonSize = ScaledButtonSize(bindingView, 112.0F, 32.0F);
        const float controlHeight = (std::max)({ deviceButtonSize.y, bindingButtonSize.y,
            ImGui::GetFrameHeight(), 32.0F * g_uiScale });
        const auto* style = ImGui::GetStyle();
        const float spacing = style ? style->ItemSpacing.x : 8.0F * g_uiScale;
        const float checkMark = ImGui::GetFrameHeight();
        const float checkWidth = MeasureText(conflictsOnly).x + MeasureText(overlapsOnly).x +
            checkMark * 2.0F + spacing * 3.0F;
        const float viewWidth = deviceButtonSize.x + bindingButtonSize.x + spacing;
        const float toolbarHeight = controlHeight + 20.0F * g_uiScale;

        ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.015F, 0.018F, 0.022F, 0.98F));
        ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.42F, 0.48F, 0.56F, 0.62F));
        ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.075F, 0.085F, 0.105F, 1.0F));
        ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(0.11F, 0.13F, 0.16F, 1.0F));
        ImGui::PushStyleColor(ImGuiCol_FrameBgActive, ImVec4(0.13F, 0.15F, 0.19F, 1.0F));
        ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 5.0F * g_uiScale);
        ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.5F);
        const bool visible = ImGui::BeginChild("BindingFilterToolbar", ImVec2(0.0F, toolbarHeight), false,
            ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
        if (visible) {
            ImVec2 toolbarAvailable{};
            ImGui::GetContentRegionAvail(&toolbarAvailable);
            ImGui::SetCursorPosY(ImGui::GetCursorPosY() +
                (std::max)(0.0F, (toolbarAvailable.y - controlHeight) * 0.5F));
        }
        if (visible && ImGui::BeginTable("BindingFilterToolbarLayout", 4,
                ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_NoPadOuterX)) {
            ImGui::TableSetupColumn("View", ImGuiTableColumnFlags_WidthFixed, viewWidth);
            ImGui::TableSetupColumn("Device", ImGuiTableColumnFlags_WidthFixed, 190.0F * g_uiScale);
            ImGui::TableSetupColumn("Search", ImGuiTableColumnFlags_WidthStretch, 1.0F);
            ImGui::TableSetupColumn("Checks", ImGuiTableColumnFlags_WidthFixed, checkWidth);
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            const bool deviceViewSelected = g_categoryMapView;
            if (deviceViewSelected) {
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.10F, 0.55F, 0.65F, 1.0F));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.14F, 0.64F, 0.74F, 1.0F));
                ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.08F, 0.47F, 0.57F, 1.0F));
            }
            if (ImGui::Button(deviceView, deviceButtonSize)) g_categoryMapView = true;
            if (deviceViewSelected) ImGui::PopStyleColor(3);
            ImGui::SameLine();
            const bool bindingViewSelected = !g_categoryMapView;
            if (bindingViewSelected) {
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.10F, 0.55F, 0.65F, 1.0F));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.14F, 0.64F, 0.74F, 1.0F));
                ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.08F, 0.47F, 0.57F, 1.0F));
            }
            if (ImGui::Button(bindingView, bindingButtonSize)) g_categoryMapView = false;
            if (bindingViewSelected) ImGui::PopStyleColor(3);

            ImGui::TableSetColumnIndex(1);
            ImGui::SetNextItemWidth(-1.0F);
            ImGui::Combo("##binding-device", &g_device, deviceItems, static_cast<int>(std::size(deviceItems)));
            ImGui::TableSetColumnIndex(2);
            ImGui::SetNextItemWidth(-1.0F);
            ImGui::InputTextWithHint("##hotkey-search", UiText("Search action, mod, or key...",
                "기능, 모드 또는 키 검색...", "搜索功能、模组或按键..."), g_search, sizeof(g_search));
            ImGui::TableSetColumnIndex(3);
            ImGui::Checkbox(conflictsOnly, &g_conflictsOnly);
            ImGui::SameLine();
            ImGui::Checkbox(overlapsOnly, &g_overlapsOnly);
            ImGui::EndTable();
        }
        ImGui::EndChild();
        ImGui::PopStyleVar(2);
        ImGui::PopStyleColor(5);
    }

    void RenderBindingTable(const std::vector<UHI::HotkeyViewGroup>& groups)
    {
        // Keep list typography tied directly to the persistent UI-scale option.
        ImGui::SetWindowFontScale(g_uiScale);
        constexpr int columnCount = 11;
        const auto flags = ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable |
            ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_ScrollY | ImGuiTableFlags_BordersInnerV |
            ImGuiTableFlags_Sortable;
        ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(10.0F * g_uiScale, 4.0F * g_uiScale));
        if (!ImGui::BeginTable("CategoryBindings", columnCount, flags, ImVec2(0.0F, 0.0F))) {
            ImGui::PopStyleVar();
            return;
        }
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn(UiText("Key", "키", "按键"),
            ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_DefaultSort, 125.0F * g_uiScale);
        ImGui::TableSetupColumn(UiText("Action", "기능", "功能"), ImGuiTableColumnFlags_WidthStretch, 2.25F);
        ImGui::TableSetupColumn(UiText("Owner / mod", "모드", "所属模组"), ImGuiTableColumnFlags_WidthStretch, 1.75F);
        ImGui::TableSetupColumn(UiText("Source type", "출처 유형", "来源类型"),
            ImGuiTableColumnFlags_WidthFixed, 180.0F * g_uiScale);
        ImGui::TableSetupColumn(UiText("Device", "장치", "设备"), ImGuiTableColumnFlags_WidthFixed, 100.0F * g_uiScale);
        ImGui::TableSetupColumn(UiText("Active in", "활성 컨텍스트", "生效上下文"),
            ImGuiTableColumnFlags_WidthFixed, 145.0F * g_uiScale);
        ImGui::TableSetupColumn(UiText("Access", "변경", "权限"), ImGuiTableColumnFlags_WidthFixed, 180.0F * g_uiScale);
        ImGui::TableSetupColumn(UiText("Confidence", "신뢰도", "可信度"), ImGuiTableColumnFlags_WidthFixed, 85.0F * g_uiScale);
        ImGui::TableSetupColumn(UiText("Conflict", "충돌", "冲突"),
            ImGuiTableColumnFlags_WidthFixed, 110.0F * g_uiScale);
        ImGui::TableSetupColumn(UiText("Overlapping action", "중복 기능", "重叠功能"),
            ImGuiTableColumnFlags_WidthStretch, 1.55F);
        ImGui::TableSetupColumn(UiText("Overlapping mod", "중복 모드", "重叠模组"),
            ImGuiTableColumnFlags_WidthStretch, 1.60F);
        ImGui::TableHeadersRow();

        const auto peerValues = [](const UHI::HotkeyViewEntry& item, const bool owners) {
            const auto& peers = item.confirmedPeers.empty() ? item.conditionalPeers : item.confirmedPeers;
            std::vector<std::string> values;
            values.reserve(peers.size());
            for (const auto* peer : peers) {
                if (!peer) continue;
                values.push_back(owners ? peer->owner : DisplayAction(*peer));
            }
            std::ranges::sort(values);
            const auto unique = std::ranges::unique(values);
            values.erase(unique.begin(), unique.end());
            std::string result;
            for (const auto& value : values) {
                if (!result.empty()) result += ", ";
                result += value;
            }
            return result;
        };

        std::vector<const UHI::HotkeyViewEntry*> rows;
        for (const auto& group : groups) {
            for (const auto& item : group.entries) rows.push_back(&item);
        }
        if (const auto* sortSpecs = ImGui::TableGetSortSpecs(); sortSpecs && sortSpecs->SpecsCount > 0) {
            const auto& spec = sortSpecs->Specs[0];
            const auto sortValue = [&](const UHI::HotkeyViewEntry& item) -> std::string {
                const auto& record = *item.record;
                constexpr int conflictColumn = 8;
                if (spec.ColumnIndex == conflictColumn) {
                    return item.conflict ? "0-conflict" : item.conditionalConflict ? "1-potential" : "2-none";
                }
                if (spec.ColumnIndex == conflictColumn + 1) {
                    return UHI::NormalizeBinding(peerValues(item, false));
                }
                if (spec.ColumnIndex == conflictColumn + 2) {
                    return UHI::NormalizeBinding(peerValues(item, true));
                }
                switch (spec.ColumnIndex) {
                case 0: return UHI::NormalizeBinding(item.displayBinding);
                case 1: return UHI::NormalizeBinding(DisplayAction(record));
                case 2: return UHI::NormalizeBinding(record.owner);
                case 3: return UHI::NormalizeBinding(SourceTypeLabel(record));
                case 4: return UHI::NormalizeBinding(record.device);
                case 5: return UHI::NormalizeBinding(LocalizedContextLabel(record.contextMask, true));
                case 6: return record.editable ? "0-editable" : "1-readonly";
                case 7: return std::to_string(static_cast<unsigned>(record.confidence));
                case 8: return item.conflict ? "0-conflict" : item.conditionalConflict ? "1-overlap" : "2-none";
                default: return {};
                }
            };
            std::stable_sort(rows.begin(), rows.end(), [&](const auto* left, const auto* right) {
                const auto lhs = sortValue(*left);
                const auto rhs = sortValue(*right);
                if (lhs == rhs) return ActionIdentity(*left->record) < ActionIdentity(*right->record);
                return spec.SortDirection == ImGuiSortDirection_Descending ? lhs > rhs : lhs < rhs;
            });
            const_cast<ImGuiTableSortSpecs*>(sortSpecs)->SpecsDirty = false;
        }

        bool anyRows = false;
        for (const auto* row : rows) {
                const auto& item = *row;
                anyRows = true;
                const auto& record = *item.record;
                ImGui::PushID(static_cast<const void*>(&record));
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                const ImVec4 bindingColor = CategoryTextColor(item.category);
                const auto friendlyBinding = FriendlyBindingLabel(item.displayBinding);
                ImGui::TextColored(bindingColor, "%s", friendlyBinding.c_str());
                if (ImGui::IsItemHovered()) VectorBindingTooltip(&item);

                ImGui::TableSetColumnIndex(1);
                const auto action = DisplayAction(record);
                ImGui::TextUnformatted(action.c_str());
                ImGui::SameLine();
                const char* renameLabel = UiText("Edit", "변경", "编辑");
                // Keep the complete action name visible. The edit button now
                // follows the natural end of the text instead of forcing the
                // label into a reserved, ellipsized slot at the column edge.
                const std::string renameButtonId = std::string(renameLabel) + "##RenameAction";
                if (ImGui::SmallButton(renameButtonId.c_str())) BeginActionRename(record);
                ImGui::TableSetColumnIndex(2);
                ImGui::TextUnformatted(record.owner.c_str());
                ImGui::TableSetColumnIndex(3);
                ImGui::TextUnformatted(SourceTypeLabel(record));
                ImGui::TableSetColumnIndex(4);
                ImGui::TextDisabled("%s", record.device.c_str());
                ImGui::TableSetColumnIndex(5);
                const auto context = LocalizedContextLabel(record.contextMask, true);
                ImGui::TextUnformatted(context.c_str());
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("%s\n%s: %s", LocalizedContextLabel(record.contextMask).c_str(),
                        UiText("Context confidence", "컨텍스트 신뢰도", "上下文可信度"),
                        ContextConfidenceName(record.contextConfidence));
                }
                ImGui::TableSetColumnIndex(6);
                ImGui::TextColored(record.editable ? ImVec4(0.42F, 0.84F, 0.66F, 1.0F) :
                    ImVec4(0.94F, 0.96F, 0.99F, 1.0F), "%s",
                    record.editable ? UiText("editable", "변경 가능", "可修改") : UiText("read-only", "읽기 전용", "只读"));
                if (record.editable && record.detector != "BuiltInHotkeyTable") {
                    ImGui::SameLine();
                    const std::string bindingButtonId = std::string(UiText("Edit", "변경", "修改")) +
                        "##ChangeBinding";
                    if (ImGui::SmallButton(bindingButtonId.c_str())) BeginBindingEdit(record);
                }
                ImGui::TableSetColumnIndex(7);
                ImGui::TextDisabled("%s", ConfidenceName(record.confidence));
                ImGui::TableSetColumnIndex(8);
                if (item.conflict) {
                    ImGui::TextColored(ImVec4(1.0F, 0.40F, 0.34F, 1.0F), "%s",
                        UiText("Conflict", "충돌", "冲突"));
                    if (ImGui::IsItemHovered()) {
                        ImGui::SetTooltip("%s", UiText(
                            "The same physical input is assigned to different actions in overlapping contexts.",
                            "같은 물리 입력이 겹치는 컨텍스트의 서로 다른 기능에 할당되어 있습니다.",
                            "同一物理输入在重叠上下文中被分配给不同功能。"));
                    }
                } else if (item.conditionalConflict) {
                    ImGui::TextColored(ImVec4(1.0F, 0.72F, 0.25F, 1.0F), "%s",
                        UiText("Overlap", "중복", "重叠"));
                    if (ImGui::IsItemHovered()) {
                        ImGui::SetTooltip("%s", UiText(
                            "The same key was found, but overlapping activation contexts are not proven.",
                            "같은 키가 발견됐지만 활성 컨텍스트가 겹치는지는 아직 증명되지 않았습니다.",
                            "检测到相同按键，但尚未证明生效上下文重叠。"));
                    }
                } else {
                    ImGui::TextDisabled("-");
                }
                constexpr int conflictColumn = 8;
                const auto conflictingActions = peerValues(item, false);
                const auto conflictingOwners = peerValues(item, true);
                ImGui::TableSetColumnIndex(conflictColumn + 1);
                if (conflictingActions.empty()) ImGui::TextDisabled("-");
                else ImGui::TextUnformatted(conflictingActions.c_str());
                ImGui::TableSetColumnIndex(conflictColumn + 2);
                if (conflictingOwners.empty()) ImGui::TextDisabled("-");
                else ImGui::TextUnformatted(conflictingOwners.c_str());
                ImGui::PopID();
        }
        if (!anyRows) {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextDisabled("%s", UiText("No results", "결과 없음", "无结果"));
            ImGui::TableSetColumnIndex(1);
            ImGui::TextDisabled("%s", UiText("Run Scanner, or change the current filters.",
                "스캔을 실행하거나 필터를 변경하세요.", "请运行扫描或更改筛选条件。"));
        }
        ImGui::EndTable();
        ImGui::PopStyleVar();
    }

    std::string TrimEditorValue(std::string value)
    {
        const auto space = [](const unsigned char ch) { return std::isspace(ch) != 0; };
        value.erase(value.begin(), std::find_if_not(value.begin(), value.end(), space));
        value.erase(std::find_if_not(value.rbegin(), value.rend(), space).base(), value.end());
        return value;
    }

    void RenderEditorPopups()
    {
        const std::string renamePopupTitle = std::string(UiText(
            "Rename action", "기능 명칭 변경", "重命名功能")) + "###UHM_RENAME_ACTION";
        const std::string bindingPopupTitle = std::string(UiText(
            "Change hotkey", "단축키 변경", "修改快捷键")) + "###UHM_CHANGE_BINDING";
        if (g_openBindingContextPopup) ImGui::OpenPopup("##UHM_BINDING_CONTEXT");
        EditorModal contextRequest = EditorModal::none;
        std::optional<UHI::HotkeyRecord> contextRequestRecord;
        const bool bindingContextVisible = ImGui::BeginPopup("##UHM_BINDING_CONTEXT");
        if (bindingContextVisible) {
            // Do not consume a popup request until Menu Framework confirms it
            // is actually visible.  Requests made from vector hotspots can be
            // deferred by the framework's popup stack for one or more frames.
            g_openBindingContextPopup = false;
            if (ConsumePopupCancelRequest()) {
                ImGui::CloseCurrentPopup();
                g_bindingContextPopupActive = false;
                g_modalInputActive = false;
                g_bindingHotspotBlockedUntilFrame = ImGui::GetFrameCount() + 2;
                g_contextRecord.reset();
            } else if (g_contextRecord) {
                const ImVec4 contextHeadingColor(0.68F, 0.72F, 0.78F, 1.0F);
                ImGui::TextColored(contextHeadingColor, "%s", g_contextRecord->owner.c_str());
                const auto contextAction = DisplayAction(*g_contextRecord);
                ImGui::TextColored(contextHeadingColor, "%s", contextAction.c_str());
                ImGui::Separator();
                if (ImGui::Selectable(UiText("Rename action", "명칭 변경", "重命名功能"), false)) {
                    contextRequestRecord = *g_contextRecord;
                    contextRequest = EditorModal::rename;
                    ImGui::CloseCurrentPopup();
                }
                const bool canEdit = g_contextRecord->editable &&
                    g_contextRecord->detector != "BuiltInHotkeyTable";
                if (ImGui::Selectable(UiText("Change hotkey", "단축키 변경", "修改快捷键"), false,
                        canEdit ? ImGuiSelectableFlags_None : ImGuiSelectableFlags_Disabled)) {
                    contextRequestRecord = *g_contextRecord;
                    contextRequest = EditorModal::binding;
                    ImGui::CloseCurrentPopup();
                }
            }
            ImGui::EndPopup();
        }
        if (contextRequest != EditorModal::none && contextRequestRecord) {
            // Open an editor only after the context popup has ended.  This
            // keeps the two commands on separate popup stacks and guarantees
            // that Change hotkey cannot inherit the rename request.
            g_openBindingContextPopup = false;
            g_bindingContextPopupActive = false;
            g_bindingHotspotBlockedUntilFrame = ImGui::GetFrameCount() + 2;
            g_contextRecord.reset();
            if (contextRequest == EditorModal::rename) BeginActionRename(*contextRequestRecord);
            else BeginBindingEdit(*contextRequestRecord);
        } else if (!bindingContextVisible && !g_openBindingContextPopup &&
            !ImGui::IsPopupOpen("##UHM_BINDING_CONTEXT")) {
            g_bindingContextPopupActive = false;
            if (g_editorModal == EditorModal::none) g_modalInputActive = false;
            g_contextRecord.reset();
        }

        const int editorFrame = ImGui::GetFrameCount();
        if (g_openRenamePopup && editorFrame >= g_editorPopupOpenNotBeforeFrame) {
            ImGui::OpenPopup(renamePopupTitle.c_str());
        }
        bool renameOpen = true;
        // Reserve enough vertical room for large UI scales so the centered
        // button row and its bottom breathing room are never clipped.
        ImGui::SetNextWindowSize(ImVec2(820.0F * g_uiScale, 400.0F * g_uiScale), ImGuiCond_Appearing);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,
            ImVec2(32.0F * g_uiScale, 28.0F * g_uiScale));
        ImGui::PushStyleColor(ImGuiCol_ModalWindowDimBg, ImVec4(0.0F, 0.0F, 0.0F, 0.0F));
        const bool renameVisible = ImGui::BeginPopupModal(renamePopupTitle.c_str(), &renameOpen,
            ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
        ImGui::PopStyleColor();
        ImGui::PopStyleVar();
        if (renameVisible) {
            g_openRenamePopup = false;
            SKSE::log::debug("Rename-action popup opened");
            if (g_editorModal != EditorModal::rename || !g_editorRecord) {
                ImGui::CloseCurrentPopup();
            } else if (ConsumePopupCancelRequest()) {
                ImGui::CloseCurrentPopup();
                CloseEditorModal();
            } else {
                ImGui::Dummy(ImVec2(0.0F, 12.0F * g_uiScale));
                const std::string renameModLine = std::string(UiText("Mod", "모드", "模组")) +
                    ": " + g_editorRecord->owner;
                const std::string renameKeyLine = std::string(UiText("Key", "키", "按键")) +
                    ": " + g_editorRecord->binding;
                RenderCenteredTextLine(renameModLine);
                RenderCenteredTextLine(renameKeyLine);
                ImGui::Dummy(ImVec2(0.0F, 22.0F * g_uiScale));
                ImVec2 renameAvailable{};
                ImGui::GetContentRegionAvail(&renameAvailable);
                const float renameInputWidth = (std::min)(renameAvailable.x, 680.0F * g_uiScale);
                ImGui::SetCursorPosX(ImGui::GetCursorPosX() +
                    (std::max)(0.0F, (renameAvailable.x - renameInputWidth) * 0.5F));
                const auto renameTextSize = MeasureText(g_renameActionValue);
                const auto* renameStyle = ImGui::GetStyle();
                const float renamePaddingY = renameStyle ? renameStyle->FramePadding.y : 6.0F * g_uiScale;
                const float renamePaddingX = (std::max)(renameStyle ? renameStyle->FramePadding.x :
                    8.0F * g_uiScale, (renameInputWidth - renameTextSize.x) * 0.5F);
                ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(renamePaddingX, renamePaddingY));
                ImGui::SetNextItemWidth(renameInputWidth);
                if (g_focusRenameField) {
                    ImGui::SetKeyboardFocusHere();
                    g_focusRenameField = false;
                }
                const bool enter = ImGui::InputText("##UHM_ACTION_NAME", g_renameActionValue,
                    sizeof(g_renameActionValue), ImGuiInputTextFlags_EnterReturnsTrue |
                    ImGuiInputTextFlags_AutoSelectAll);
                ImGui::PopStyleVar();
                const auto applyRename = [&]() {
                    auto name = TrimEditorValue(g_renameActionValue);
                    if (name.empty()) return;
                    g_actionNameOverrides.insert_or_assign(ActionIdentity(*g_editorRecord), std::move(name));
                    SaveActionOverrides();
                    ImGui::CloseCurrentPopup();
                    CloseEditorModal();
                };
                if (enter) applyRename();
                ImGui::Dummy(ImVec2(0.0F, 34.0F * g_uiScale));
                const char* applyLabel = UiText("Apply", "적용", "应用");
                const char* automaticLabel = UiText("Use automatic name", "자동 명칭 복원", "恢复自动名称");
                const char* cancelLabel = UiText("Cancel", "취소", "取消");
                const auto applySize = ScaledButtonSize(applyLabel, 110.0F, 38.0F);
                const auto automaticSize = ScaledButtonSize(automaticLabel, 170.0F, 38.0F);
                const auto cancelSize = ScaledButtonSize(cancelLabel, 110.0F, 38.0F);
                const auto* popupStyle = ImGui::GetStyle();
                const float popupSpacing = popupStyle ? popupStyle->ItemSpacing.x : 8.0F * g_uiScale;
                const float buttonGroupWidth = applySize.x + automaticSize.x + cancelSize.x + popupSpacing * 2.0F;
                ImVec2 popupAvailable{};
                ImGui::GetContentRegionAvail(&popupAvailable);
                ImGui::SetCursorPosX(ImGui::GetCursorPosX() +
                    (std::max)(0.0F, (popupAvailable.x - buttonGroupWidth) * 0.5F));
                if (ImGui::Button(applyLabel, applySize)) applyRename();
                ImGui::SameLine();
                if (ImGui::Button(automaticLabel, automaticSize)) {
                    g_actionNameOverrides.erase(ActionIdentity(*g_editorRecord));
                    SaveActionOverrides();
                    ImGui::CloseCurrentPopup();
                    CloseEditorModal();
                }
                ImGui::SameLine();
                if (ImGui::Button(cancelLabel, cancelSize)) {
                    ImGui::CloseCurrentPopup();
                    CloseEditorModal();
                }
                ImGui::Dummy(ImVec2(0.0F, 24.0F * g_uiScale));
            }
            ImGui::EndPopup();
        }
        if (!renameOpen && g_editorModal == EditorModal::rename) CloseEditorModal();

        if (g_openBindingPopup && editorFrame >= g_editorPopupOpenNotBeforeFrame) {
            ImGui::OpenPopup(bindingPopupTitle.c_str());
        }
        bool bindingOpen = true;
        // The format description and two-line capture instructions can wrap
        // at 125-135% scale.  Keep the action buttons visible and leave a
        // deliberate lower margin instead of clipping the modal contents.
        ImGui::SetNextWindowSize(ImVec2(880.0F * g_uiScale, 520.0F * g_uiScale), ImGuiCond_Appearing);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,
            ImVec2(32.0F * g_uiScale, 28.0F * g_uiScale));
        ImGui::PushStyleColor(ImGuiCol_ModalWindowDimBg, ImVec4(0.0F, 0.0F, 0.0F, 0.0F));
        const bool bindingVisible = ImGui::BeginPopupModal(bindingPopupTitle.c_str(), &bindingOpen,
            ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
        ImGui::PopStyleColor();
        ImGui::PopStyleVar();
        if (bindingVisible) {
            g_openBindingPopup = false;
            SKSE::log::debug("Change-hotkey popup opened");
            if (g_editorModal != EditorModal::binding || !g_editorRecord) {
                ImGui::CloseCurrentPopup();
            } else if (ConsumePopupCancelRequest()) {
                ImGui::CloseCurrentPopup();
                CloseEditorModal();
            } else {
                const auto writeResult = g_bindingWriteResult.exchange(0);
                if (writeResult == 1) {
                    ImGui::CloseCurrentPopup();
                    CloseEditorModal();
                    ImGui::EndPopup();
                    return;
                }
                if (writeResult == 2) {
                    std::scoped_lock resultLock(g_bindingWriteResultMutex);
                    g_bindingCaptureStatus = g_bindingWriteResultMessage;
                }
                const auto& record = *g_editorRecord;
                ImGui::Dummy(ImVec2(0.0F, 30.0F * g_uiScale));
                const auto action = DisplayAction(record);
                const std::string bindingModLine = std::string(UiText("Mod", "모드", "模组")) +
                    ": " + record.owner;
                const std::string bindingActionLine = std::string(UiText("Action", "기능", "功能")) +
                    ": " + action;
                const std::string bindingFormatLine = std::string(UiText(
                    "Storage format", "저장 형식", "存储格式")) + ": " + record.codeSystem;
                RenderCenteredTextLine(bindingModLine);
                RenderCenteredTextLine(bindingActionLine);
                RenderCenteredTextLine(bindingFormatLine);
                ImGui::Dummy(ImVec2(0.0F, 18.0F * g_uiScale));
                const bool capturing = g_bindingCaptureActive.load();
                const std::string captureText = capturing ? UiText(
                    "Press a keyboard, mouse or gamepad input...",
                    "키보드·마우스·게임패드 입력을 누르세요...",
                    "请按下键盘、鼠标或手柄输入...") :
                    (g_bindingCaptureDisplay.empty() ? record.binding : g_bindingCaptureDisplay);
                ImVec2 captureAvailable{};
                ImGui::GetContentRegionAvail(&captureAvailable);
                const float captureWidth = (std::min)(captureAvailable.x, 720.0F * g_uiScale);
                ImGui::SetCursorPosX(ImGui::GetCursorPosX() +
                    (std::max)(0.0F, (captureAvailable.x - captureWidth) * 0.5F));
                if (capturing) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.12F, 0.43F, 0.53F, 1.0F));
                if (ImGui::Button(captureText.c_str(), ImVec2(captureWidth, 46.0F * g_uiScale))) {
                    g_bindingCaptureStatus.clear();
                    g_bindingCaptureActive = true;
                    g_bindingCaptureArmedFrame = ImGui::GetFrameCount();
                }
                if (capturing) ImGui::PopStyleColor();
                if (g_bindingCaptureActive.load()) PollImGuiBindingCapture();
                RenderCenteredTextLines(UiText(
                    "Click the field, then press one input\nor one modifier plus an input.",
                    "필드를 누른 뒤 단일 입력 또는\n모디파이어 하나와 입력 하나를 누르세요.",
                    "点击输入框后，按一个输入，\n或一个修饰键加一个输入。"));
                if (!g_bindingCaptureStatus.empty()) {
                    const ImVec4 statusColor(1.0F, 0.64F, 0.35F, 1.0F);
                    RenderCenteredTextLines(g_bindingCaptureStatus, &statusColor);
                }
                ImGui::Dummy(ImVec2(0.0F, 28.0F * g_uiScale));
                const char* saveLabel = UiText("Save change", "변경 저장", "保存修改");
                const char* cancelLabel = UiText("Cancel", "취소", "取消");
                const auto saveSize = ScaledButtonSize(saveLabel, 140.0F, 38.0F);
                const auto cancelSize = ScaledButtonSize(cancelLabel, 110.0F, 38.0F);
                const auto* popupStyle = ImGui::GetStyle();
                const float popupSpacing = popupStyle ? popupStyle->ItemSpacing.x : 8.0F * g_uiScale;
                ImVec2 popupAvailable{};
                ImGui::GetContentRegionAvail(&popupAvailable);
                ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (std::max)(0.0F,
                    (popupAvailable.x - saveSize.x - cancelSize.x - popupSpacing) * 0.5F));
                const bool writePending = g_bindingWritePending.load();
                if (g_bindingCaptureRaw.empty() || writePending) ImGui::BeginDisabled();
                if (ImGui::Button(saveLabel, saveSize)) {
                    UHI::BindingWriter writer;
                    {
                        std::scoped_lock statusLock(g_statusMutex);
                        writer = g_bindingWriter;
                    }
                    if (!writer) {
                        g_bindingCaptureStatus = UiText(
                            "The binding writer is unavailable. No value was changed.",
                            "단축키 저장기가 준비되지 않았습니다. 값은 변경되지 않았습니다.",
                            "快捷键写入器不可用，未修改任何值。");
                    } else {
                        g_bindingWritePending = true;
                        g_bindingWriteResult = 0;
                        g_bindingCaptureStatus = UiText(
                            "Saving the source value and synchronizing the active MCM...",
                            "원본 값을 저장하고 활성 MCM을 동기화하는 중입니다...",
                            "正在保存源值并同步当前 MCM...");
                        writer(record, g_bindingCaptureRaw,
                            [](const bool success, std::string message) {
                                BindingWriteNotice notice{ BindingWriteNotice::none };
                                if (message == "UHM_MCM_GAME_SAVE_REQUIRED") {
                                    notice = BindingWriteNotice::gameSaveRequired;
                                } else if (message == "UHM_MCM_DOCUMENT_ROLLED_BACK") {
                                    notice = BindingWriteNotice::documentRolledBack;
                                } else if (message == "UHM_MCM_DOCUMENT_ROLLBACK_UNVERIFIED") {
                                    notice = BindingWriteNotice::documentRollbackUnverified;
                                }
                                {
                                    std::scoped_lock resultLock(g_bindingWriteResultMutex);
                                    g_bindingWriteResultMessage = std::move(message);
                                }
                                g_bindingWritePending = false;
                                if (notice != BindingWriteNotice::none) {
                                    g_pendingBindingWriteNotice.store(static_cast<int>(notice));
                                    // Close the editor first. The dedicated
                                    // undimmed notice opens on the following
                                    // frame so two modal stacks never compete.
                                    g_bindingWriteResult = 1;
                                } else {
                                    g_bindingWriteResult = success ? 1 : 2;
                                }
                            });
                    }
                }
                if (g_bindingCaptureRaw.empty() || writePending) ImGui::EndDisabled();
                ImGui::SameLine();
                if (ImGui::Button(cancelLabel, cancelSize)) {
                    ImGui::CloseCurrentPopup();
                    CloseEditorModal();
                }
                ImGui::Dummy(ImVec2(0.0F, 10.0F * g_uiScale));
            }
            ImGui::EndPopup();
        }
        if (!bindingOpen && g_editorModal == EditorModal::binding) CloseEditorModal();
    }

    void __stdcall RenderHotkeyView()
    {
        if (!HasMenuFrameworkContext()) {
            return;
        }
        g_escapeConsumedByPopupThisFrame = false;
        g_renderWindowVisible = true;
        // Hold one immutable result snapshot for the complete ImGui frame. A
        // worker may publish a newer scan without invalidating entry pointers.
        std::shared_ptr<const RegistrySnapshot> snapshot;
        {
            std::scoped_lock lock(g_registryMutex);
            snapshot = g_registrySnapshot;
        }
        const auto registry = snapshot ? snapshot->registry : nullptr;
        const auto* io = ImGui::GetIO();
        if (io) {
            // Open at the large, centered working size used by the full device
            // overview. The former 1800x1050 ceiling made the window look tiny
            // at 4K. Appearing resets only when the window is opened; users can
            // still resize it freely for the remainder of that session.
            const float width = (std::min)(
                std::clamp(io->DisplaySize.x * 0.77F, 1280.0F, 3400.0F),
                io->DisplaySize.x * 0.96F);
            const float height = (std::min)(
                std::clamp(io->DisplaySize.y * 0.86F, 800.0F, 2000.0F),
                io->DisplaySize.y * 0.94F);
            ImGui::SetNextWindowSize(ImVec2(width, height), ImGuiCond_Appearing);
            ImGui::SetNextWindowPos(ImVec2(io->DisplaySize.x * 0.5F, io->DisplaySize.y * 0.5F),
                ImGuiCond_Appearing, ImVec2(0.5F, 0.5F));
        }
        if (g_focusWindowOnNextRender.exchange(false)) {
            ImGui::SetNextWindowFocus();
        }
        // Reaching this callback is itself authoritative evidence that Menu
        // Framework is presenting the UHM window.  Some framework builds lag
        // WindowInterface::IsOpen by one frame, so seeding `open` from that
        // public flag can immediately discard the render-visible latch and
        // make Escape fall through to Skyrim's system menu.
        bool open = true;
        const float layoutScale = std::clamp(g_uiScale, 0.80F, 1.35F);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.5F * layoutScale);
        ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, 0.5F * layoutScale);
        ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0F);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 8.0F * layoutScale);
        ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 6.0F * layoutScale);
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 4.0F * layoutScale);
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(10.0F * layoutScale, 6.0F * layoutScale));
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(9.0F * layoutScale, 7.0F * layoutScale));
        ImGui::PushStyleVar(ImGuiStyleVar_TabBorderSize, 1.0F);
        ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.36F, 0.40F, 0.46F, 0.42F));
        ImGui::PushStyleColor(ImGuiCol_Separator, ImVec4(0.36F, 0.40F, 0.46F, 0.32F));
        // Device-map children should visually share the main window background.
        // A transparent child background preserves the configured window opacity
        // and avoids a second blue-grey layer behind the keyboard/mouse/gamepad.
        ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.0F, 0.0F, 0.0F, 0.0F));
        ImGui::PushStyleColor(ImGuiCol_Tab, ImVec4(0.08F, 0.10F, 0.13F, 0.92F));
        ImGui::PushStyleColor(ImGuiCol_TabHovered, ImVec4(0.18F, 0.39F, 0.49F, 0.96F));
        ImGui::PushStyleColor(ImGuiCol_TabActive, ImVec4(0.14F, 0.48F, 0.54F, 1.0F));
        ImGui::PushStyleColor(ImGuiCol_TabUnfocused, ImVec4(0.07F, 0.08F, 0.11F, 0.88F));
        ImGui::PushStyleColor(ImGuiCol_TabUnfocusedActive, ImVec4(0.12F, 0.38F, 0.44F, 0.96F));
        ImGui::PushStyleColor(ImGuiCol_TextDisabled, ImVec4(0.94F, 0.96F, 0.99F, 1.0F));
        ImGui::PushStyleColor(ImGuiCol_TitleBg, ImVec4(0.0F, 0.0F, 0.0F, 1.0F));
        ImGui::PushStyleColor(ImGuiCol_TitleBgActive, ImVec4(0.0F, 0.0F, 0.0F, 1.0F));
        ImGui::PushStyleColor(ImGuiCol_TitleBgCollapsed, ImVec4(0.0F, 0.0F, 0.0F, 1.0F));
        const std::string windowTitle = "Universal Hotkey Manager for Skyrim SE-AE##UniversalHotkeyManagerV2";
        // Apply opacity only to the main window background. This deliberately
        // does not touch global style alpha, child contents, controls, text,
        // device drawings, or modal popup backgrounds.
        ImGui::SetNextWindowBgAlpha(std::clamp(g_windowOpacity, 0.35F, 1.0F));
        ImGui::Begin(windowTitle.c_str(), &open, 0);
        ImGui::SetWindowFontScale(g_uiScale);
        // Opening the manager must remain immediate and deterministic.  The
        // inexpensive cached snapshot is shown here; automatic incremental
        // refresh runs after a completed save load instead of surprising the
        // user with a late modal while they are already using this window.
        if (open) g_automaticRefreshRequestedForCurrentOpen = true;
        if (g_window && !open) {
            g_renderWindowVisible = false;
            g_openingHotkeyCaptureActive = false;
            g_visibleChangedHotkeyNotice = 0U;
            SavePendingPreferences();
            g_window->IsOpen = false;
            g_automaticRefreshRequestedForCurrentOpen = false;
        }
        if (const auto changed = g_pendingChangedHotkeyNotice.exchange(0U); changed > 0U) {
            g_visibleChangedHotkeyNotice = changed;
        }
        // Keep requesting the modal until ImGui confirms it is visible.  A
        // one-frame OpenPopup request can be lost while Menu Framework is
        // changing tabs or closing another popup.
        if (g_visibleChangedHotkeyNotice > 0U &&
            !ImGui::IsPopupOpen("##UHM_CHANGED_HOTKEY_NOTICE")) {
            ImGui::OpenPopup("##UHM_CHANGED_HOTKEY_NOTICE");
        }
        bool changedNoticeOpen = true;
        ImGui::SetNextWindowSize(ImVec2(620.0F * g_uiScale, 230.0F * g_uiScale), ImGuiCond_Appearing);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,
            ImVec2(30.0F * g_uiScale, 26.0F * g_uiScale));
        ImGui::PushStyleColor(ImGuiCol_ModalWindowDimBg, ImVec4(0.0F, 0.0F, 0.0F, 0.0F));
        if (ImGui::BeginPopupModal("##UHM_CHANGED_HOTKEY_NOTICE", &changedNoticeOpen, 0)) {
            g_modalInputActive = true;
            const bool dismissNotice = ConsumePopupCancelRequest();
            ImGui::Dummy(ImVec2(0.0F, 18.0F * g_uiScale));
            char noticeText[256]{};
            std::snprintf(noticeText, sizeof(noticeText), UiText(
                "%zu hotkeys were added or changed.",
                "단축키 %zu개가 새로 추가되었거나 변경되었습니다.",
                "%zu 个快捷键已新增或更改。"), g_visibleChangedHotkeyNotice);
            RenderCenteredTextLine(noticeText);
            ImGui::Dummy(ImVec2(0.0F, 36.0F * g_uiScale));
            const char* okay = UiText("OK", "확인", "确定");
            const auto okaySize = ScaledButtonSize(okay, 120.0F, 38.0F);
            ImVec2 noticeAvailable{};
            ImGui::GetContentRegionAvail(&noticeAvailable);
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() +
                (std::max)(0.0F, (noticeAvailable.x - okaySize.x) * 0.5F));
            if (dismissNotice || ImGui::Button(okay, okaySize)) {
                g_visibleChangedHotkeyNotice = 0U;
                g_modalInputActive = false;
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }
        ImGui::PopStyleColor();
        ImGui::PopStyleVar();
        if (!changedNoticeOpen) {
            g_visibleChangedHotkeyNotice = 0U;
            g_modalInputActive = false;
        }

        // Binding-write notices are deliberately separate from the editor.
        // Open them only after the editor (and the automatic-scan notice) has
        // closed so Menu Framework never has to resolve two modal stacks.
        if (g_editorModal == EditorModal::none && g_visibleChangedHotkeyNotice == 0U) {
            if (const auto pending = g_pendingBindingWriteNotice.exchange(0);
                pending != static_cast<int>(BindingWriteNotice::none)) {
                g_visibleBindingWriteNotice = static_cast<BindingWriteNotice>(pending);
            }
            // As above, retain the notice until the modal is actually open.
            if (g_visibleBindingWriteNotice != BindingWriteNotice::none &&
                !ImGui::IsPopupOpen("##UHM_BINDING_WRITE_NOTICE")) {
                ImGui::OpenPopup("##UHM_BINDING_WRITE_NOTICE");
            }
        }
        bool bindingNoticeOpen = true;
        ImGui::SetNextWindowSize(ImVec2(760.0F * g_uiScale, 300.0F * g_uiScale), ImGuiCond_Appearing);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,
            ImVec2(38.0F * g_uiScale, 34.0F * g_uiScale));
        ImGui::PushStyleColor(ImGuiCol_ModalWindowDimBg, ImVec4(0.0F, 0.0F, 0.0F, 0.0F));
        if (ImGui::BeginPopupModal("##UHM_BINDING_WRITE_NOTICE", &bindingNoticeOpen, 0)) {
            g_modalInputActive = true;
            const bool dismissNotice = ConsumePopupCancelRequest();
            const char* message{};
            ImVec4 messageColor(0.98F, 0.99F, 1.0F, 1.0F);
            switch (g_visibleBindingWriteNotice) {
            case BindingWriteNotice::gameSaveRequired:
                message = UiText(
                    "The Papyrus/MCM hotkey is active in the current game.\nSave the game to keep it after the next load.",
                    "Papyrus/MCM 단축키가 현재 게임에 적용되었습니다.\n게임을 저장해야 다음 로드 이후에도 유지됩니다.",
                    "Papyrus/MCM 快捷键已应用于当前游戏。\n请保存游戏，以便下次载入后继续保留。");
                break;
            case BindingWriteNotice::documentRolledBack:
                message = UiText(
                    "MCM synchronization failed, so the change was not applied.\nThe original hotkey was restored.",
                    "MCM 동기화에 실패해 변경을 적용하지 않았습니다.\n원래 단축키로 되돌아갔습니다.",
                    "MCM 同步失败，因此未应用更改。\n已恢复原来的快捷键。");
                messageColor = ImVec4(1.0F, 0.72F, 0.28F, 1.0F);
                break;
            case BindingWriteNotice::documentRollbackUnverified:
                message = UiText(
                    "MCM synchronization failed and restoration of the original hotkey could not be verified.\nCheck the original settings file before continuing.",
                    "MCM 동기화에 실패했고 원래 단축키 복구도 확인하지 못했습니다.\n계속하기 전에 원본 설정 파일을 확인하세요.",
                    "MCM 同步失败，且无法确认原快捷键是否已恢复。\n继续前请检查原始设置文件。");
                messageColor = ImVec4(1.0F, 0.40F, 0.34F, 1.0F);
                break;
            case BindingWriteNotice::fontRestartRequired:
                // Keep this first-run message ASCII-only: it is shown exactly
                // when the active atlas may not contain the selected CJK range.
                message = "Language glyph support was enabled in SKSE Menu Framework.\n"
                    "Restart Skyrim once to apply it.";
                break;
            default:
                message = UiText("The hotkey operation has completed.",
                    "단축키 작업이 완료되었습니다.", "快捷键操作已完成。");
                break;
            }
            ImGui::Dummy(ImVec2(0.0F, 34.0F * g_uiScale));
            ImGui::SetWindowFontScale(g_uiScale * 1.10F);
            std::string messageLines = message ? message : "";
            std::size_t lineStart{};
            while (lineStart <= messageLines.size()) {
                const auto newline = messageLines.find('\n', lineStart);
                const auto line = messageLines.substr(lineStart,
                    newline == std::string::npos ? std::string::npos : newline - lineStart);
                ImVec2 lineAvailable{};
                ImGui::GetContentRegionAvail(&lineAvailable);
                const auto lineSize = MeasureText(line.c_str());
                ImGui::SetCursorPosX(ImGui::GetCursorPosX() +
                    (std::max)(0.0F, (lineAvailable.x - lineSize.x) * 0.5F));
                ImGui::TextColored(messageColor, "%s", line.c_str());
                if (newline == std::string::npos) break;
                lineStart = newline + 1U;
            }
            ImGui::SetWindowFontScale(g_uiScale);
            ImGui::Dummy(ImVec2(0.0F, 48.0F * g_uiScale));
            const char* okay = UiText("OK", "확인", "确定");
            const auto okaySize = ScaledButtonSize(okay, 130.0F, 40.0F);
            ImVec2 noticeAvailable{};
            ImGui::GetContentRegionAvail(&noticeAvailable);
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() +
                (std::max)(0.0F, (noticeAvailable.x - okaySize.x) * 0.5F));
            if (dismissNotice || ImGui::Button(okay, okaySize)) {
                g_visibleBindingWriteNotice = BindingWriteNotice::none;
                g_modalInputActive = false;
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }
        ImGui::PopStyleColor();
        ImGui::PopStyleVar();
        if (!bindingNoticeOpen && g_visibleBindingWriteNotice != BindingWriteNotice::none) {
            g_visibleBindingWriteNotice = BindingWriteNotice::none;
            g_modalInputActive = false;
        }
        bool running{};
        float scanPercent{};
        float scanFilePercent{};
        std::string scanStage;
        std::string scanPath;
        std::function<void()> startScan;
        std::function<void()> cancelScan;
        std::function<UHI::OpeningHotkey()> getOpeningHotkey;
        {
            std::scoped_lock lock(g_statusMutex);
            running = g_scanRunning;
            scanPercent = g_scanPercent;
            scanFilePercent = g_scanFilePercent;
            scanStage = g_scanStage;
            scanPath = g_scanPath;
            startScan = g_startScan;
            cancelScan = g_cancelScan;
            getOpeningHotkey = g_getOpeningHotkey;
        }
        const bool hasSexLab = g_sexLabInstalled.load() || (snapshot && std::any_of(snapshot->groups.begin(), snapshot->groups.end(),
            [](const UHI::HotkeyViewGroup& group) {
                return std::any_of(group.entries.begin(), group.entries.end(),
                    [](const UHI::HotkeyViewEntry& entry) {
                        return entry.category == UHI::HotkeyCategory::sexlab;
                    });
            }));
        constexpr std::array categoryTabs{
            UHI::HotkeyCategory::game,
            UHI::HotkeyCategory::environment,
            UHI::HotkeyCategory::character,
            UHI::HotkeyCategory::combat,
            UHI::HotkeyCategory::sexlab,
            UHI::HotkeyCategory::interface,
            UHI::HotkeyCategory::external
        };
        ImGui::SetWindowFontScale(g_uiScale * 1.10F);
        const auto beginStyledTab = [&](const char* label, const UHI::HotkeyCategory category,
                                            const bool selected, const bool neutralWhite) {
            const ImVec4 inactiveText = neutralWhite ? ImVec4(0.96F, 0.97F, 0.99F, 1.0F) :
                                                       CategoryTextColor(category, 1.0F);
            const ImVec4 activeSurface = neutralWhite ? ImVec4(0.92F, 0.94F, 0.97F, 1.0F) :
                                                        CategoryKeyColor(category, 1.0F);
            const ImVec4 activeText = neutralWhite ? ImVec4(0.035F, 0.045F, 0.060F, 1.0F) :
                                                     ImVec4(0.98F, 0.99F, 1.0F, 1.0F);
            const ImVec4 hoverSurface(
                (std::min)(1.0F, activeSurface.x + 0.08F),
                (std::min)(1.0F, activeSurface.y + 0.08F),
                (std::min)(1.0F, activeSurface.z + 0.08F), 1.0F);
            ImGui::PushStyleColor(ImGuiCol_Text, selected ? activeText : inactiveText);
            ImGui::PushStyleColor(ImGuiCol_Tab, ImVec4(0.045F, 0.052F, 0.064F, 0.96F));
            ImGui::PushStyleColor(ImGuiCol_TabHovered, hoverSurface);
            ImGui::PushStyleColor(ImGuiCol_TabActive, activeSurface);
            ImGui::PushStyleColor(ImGuiCol_TabUnfocused, ImVec4(0.035F, 0.041F, 0.052F, 0.92F));
            ImGui::PushStyleColor(ImGuiCol_TabUnfocusedActive, activeSurface);
            const bool visible = ImGui::BeginTabItem(label);
            ImGui::PopStyleColor(6);
            return visible;
        };
        if (ImGui::BeginTabBar("HotkeyViewTabs", ImGuiTabBarFlags_FittingPolicyScroll)) {
            if (beginStyledTab(UiText("Overview", "오버뷰", "概览"),
                    UHI::HotkeyCategory::all, g_page == HotkeyViewPage::overview, true)) {
                g_page = HotkeyViewPage::overview;
                ImGui::EndTabItem();
            }
            for (const auto category : categoryTabs) {
                if (category == UHI::HotkeyCategory::sexlab && !hasSexLab) continue;
                if (beginStyledTab(CategoryLabel(category), category,
                        g_page == HotkeyViewPage::category && g_category == category, false)) {
                    g_page = HotkeyViewPage::category;
                    g_category = category;
                    ImGui::EndTabItem();
                }
            }
            if (beginStyledTab(UiText("Options", "옵션", "选项"),
                    UHI::HotkeyCategory::all, g_page == HotkeyViewPage::options, true)) {
                g_page = HotkeyViewPage::options;
                ImGui::EndTabItem();
            }
            ImGui::EndTabBar();
        }
        ImGui::SetWindowFontScale(g_uiScale);
        ImGui::Spacing();
        // The opening-shortcut badge belongs to the graphical device view.
        // Keep the management table unobstructed when list view is selected.
        if (g_page == HotkeyViewPage::overview) {
            const bool hasResults = snapshot && std::ranges::any_of(snapshot->groups,
                [](const UHI::HotkeyViewGroup& group) { return !group.entries.empty(); });
            std::size_t editableCount{};
            std::size_t readOnlyCount{};
            std::size_t mappedCount{};
            if (snapshot) {
                for (const auto& group : snapshot->groups) {
                    for (const auto& item : group.entries) {
                        const auto& record = *item.record;
                        ++mappedCount;
                        editableCount += record.editable ? 1U : 0U;
                        readOnlyCount += record.editable ? 0U : 1U;
                    }
                }
            }
            const char* totalLabel = UiText("BINDINGS", "단축키", "快捷键");
            const char* conflictLabel = UiText("CONFLICTS", "충돌", "冲突");
            const char* editableLabel = UiText("EDITABLE", "변경 가능", "可修改");
            const char* readOnlyLabel = UiText("READ-ONLY", "읽기 전용", "只读");
            ImVec2 overviewHeaderAvailable{};
            ImGui::GetContentRegionAvail(&overviewHeaderAvailable);
            const float overviewHeaderMargin = MeasureText(UiText("M", "가", "字")).x;
            const float overviewHeaderUsableWidth = (std::max)(0.0F,
                overviewHeaderAvailable.x - overviewHeaderMargin * 2.0F);
            const float overviewSummaryWidth = (std::min)(620.0F * g_uiScale,
                overviewHeaderUsableWidth * 0.52F);
            ImGui::Spacing();
            if (ImGui::BeginTable("OverviewHeader", 4,
                    ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_NoPadOuterX)) {
                ImGui::TableSetupColumn("LeftMargin", ImGuiTableColumnFlags_WidthFixed, overviewHeaderMargin);
                ImGui::TableSetupColumn("Description", ImGuiTableColumnFlags_WidthStretch, 1.0F);
                ImGui::TableSetupColumn("Summary", ImGuiTableColumnFlags_WidthFixed, overviewSummaryWidth);
                ImGui::TableSetupColumn("RightMargin", ImGuiTableColumnFlags_WidthFixed, overviewHeaderMargin);
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(1);
                const float headerTop = ImGui::GetCursorPosY();
                RenderScaledBoldLine(UiText("Overview", "오버뷰", "概览"), 1.50F,
                    ImVec4(0.98F, 0.99F, 1.0F, 1.0F));
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.94F, 0.96F, 0.99F, 1.0F));
                ImGui::TextWrapped("%s", UiText(
                    "A visual map of every detected keyboard, mouse, and gamepad binding.",
                    "감지된 키보드·마우스·게임패드 단축키를 한눈에 보여줍니다.",
                    "以图形方式总览检测到的键盘、鼠标与手柄快捷键。"));
                ImGui::PopStyleColor();

                ImGui::TableSetColumnIndex(2);
                ImGui::SetCursorPosY(headerTop);
                if (ImGui::BeginTable("OverviewSummaryGrid", 4,
                        ImGuiTableFlags_SizingStretchSame | ImGuiTableFlags_NoPadOuterX)) {
                    constexpr std::array<std::size_t, 4> metricIndices{ 0, 1, 2, 3 };
                    const std::array<const char*, 4> metricLabels{
                        totalLabel, conflictLabel, editableLabel, readOnlyLabel
                    };
                    const std::array<std::size_t, 4> metricValues{
                        mappedCount, snapshot ? snapshot->conflictCount : 0, editableCount, readOnlyCount
                    };
                    const std::array<ImVec4, 4> metricColors{
                        ImVec4(0.90F, 0.93F, 0.98F, 1.0F), ImVec4(1.0F, 0.42F, 0.36F, 1.0F),
                        ImVec4(0.35F, 0.85F, 0.65F, 1.0F), ImVec4(0.86F, 0.89F, 0.94F, 1.0F)
                    };
                    ImGui::TableNextRow();
                    for (const auto index : metricIndices) {
                        ImGui::TableSetColumnIndex(static_cast<int>(index));
                        ImVec2 cellAvailable{};
                        ImGui::GetContentRegionAvail(&cellAvailable);
                        ImGui::SetCursorPosX(ImGui::GetCursorPosX() +
                            (std::max)(0.0F, cellAvailable.x - MeasureText(metricLabels[index]).x));
                        ImGui::TextUnformatted(metricLabels[index]);
                    }
                    ImGui::TableNextRow();
                    ImGui::SetWindowFontScale(g_uiScale * 1.50F);
                    for (const auto index : metricIndices) {
                        ImGui::TableSetColumnIndex(static_cast<int>(index));
                        const auto number = std::to_string(metricValues[index]);
                        ImVec2 cellAvailable{};
                        ImGui::GetContentRegionAvail(&cellAvailable);
                        ImGui::SetCursorPosX(ImGui::GetCursorPosX() +
                            (std::max)(0.0F, cellAvailable.x - MeasureText(number.c_str()).x));
                        ImGui::TextColored(metricColors[index], "%s", number.c_str());
                    }
                    ImGui::SetWindowFontScale(g_uiScale);
                    ImGui::EndTable();
                }
                ImGui::EndTable();
            }
            if (!hasResults) {
                ImGui::TextWrapped("%s", UiText(
                    "No scan results are loaded yet. Open the Options tab to inventory the active mod environment. The device map below is always available; bindings are filled in after scanning.",
                    "아직 스캔 결과가 없습니다. 옵션 탭에서 활성 모드 환경을 스캔하세요. 아래 장치 그림은 항상 표시되며 스캔 후 단축키가 채워집니다.",
                    "尚未加载扫描结果。请在选项页扫描当前模组环境。下方设备图始终可见，扫描后会填入快捷键。"));
            } else if (snapshot && snapshot->restored) {
                ImGui::TextDisabled("%s", UiText(
                    "Previous scan restored. Open Options to refresh after changing the active mod list.",
                    "이전 스캔을 복원했습니다. 활성 모드 목록을 바꿨다면 옵션에서 다시 스캔하세요.",
                    "已恢复上次扫描。更改启用模组列表后，请在选项中重新扫描。"));
            }
            ImGui::Spacing();
            RenderFilterToolbar();
            UpdateFilteredGroups(snapshot, UHI::HotkeyCategory::all);
            const bool mapVisible = ImGui::BeginChild("OverviewDeviceMap", ImVec2(0.0F, 0.0F), false);
            if (mapVisible) {
                if (g_categoryMapView) RenderVectorOverview(g_filteredGroups);
                else RenderBindingTable(g_filteredGroups);
            }
            ImGui::EndChild();
        }
        if (getOpeningHotkey) {
            std::scoped_lock lock(g_preferencesMutex);
            if (!g_openingHotkeyDraftLoaded) {
                g_openingHotkeyDraft = getOpeningHotkey();
                g_uiScale = std::clamp(g_openingHotkeyDraft.uiScale, 0.80F, 1.35F);
                g_windowOpacity = std::clamp(g_openingHotkeyDraft.windowOpacity, 0.35F, 1.0F);
                g_uiLanguage.store(g_openingHotkeyDraft.uiLanguage);
                g_openingHotkeyDraftLoaded = true;
                g_preferencesDirty = false;
            }
        }
        if (g_page == HotkeyViewPage::options) {
            ImGui::Dummy(ImVec2(0.0F, 40.0F * g_uiScale));
            UHI::OpeningHotkey preferencesDraft;
            {
                std::scoped_lock lock(g_preferencesMutex);
                preferencesDraft = g_openingHotkeyDraft;
            }

            ImVec2 optionsAvailable{};
            ImGui::GetContentRegionAvail(&optionsAvailable);
            const float optionsWidth = optionsAvailable.x;
            const float optionsSideMargin = std::clamp(optionsWidth * 0.025F,
                24.0F * g_uiScale, 56.0F * g_uiScale);
            const float optionsInnerWidth = (std::max)(320.0F * g_uiScale,
                optionsWidth - optionsSideMargin * 2.0F);
            const float optionTitleWidth = (std::min)(285.0F * g_uiScale,
                (std::max)(190.0F * g_uiScale, optionsInnerWidth * 0.145F));
            const float optionDescriptionWidth = (std::min)(520.0F * g_uiScale,
                (std::max)(320.0F * g_uiScale, optionsInnerWidth * 0.245F));
            const float optionSeparatorHeight = 10.0F * g_uiScale;
            const float optionGroupHeight = std::clamp(
                (optionsAvailable.y - optionSeparatorHeight * 2.0F - 24.0F * g_uiScale) / 3.0F,
                120.0F * g_uiScale, 220.0F * g_uiScale);
            // Scanner controls are compact enough that the first fixed-height
            // third left a large dead band before both separators. Pull that
            // first row upward while retaining the generous spacing between
            // Appearance and Opening shortcut.
            const float scannerGroupHeight = (std::max)(96.0F * g_uiScale,
                optionGroupHeight - 76.0F * g_uiScale);
            const float appearanceGroupHeight = (std::max)(154.0F * g_uiScale,
                optionGroupHeight - 8.0F * g_uiScale);
            ImGui::PushStyleVar(ImGuiStyleVar_CellPadding,
                ImVec2(24.0F * g_uiScale, 22.0F * g_uiScale));
            if (ImGui::BeginTable("OptionsThreeColumn", 5,
                    ImGuiTableFlags_SizingStretchProp)) {
                ImGui::TableSetupColumn("OptionLeftMargin", ImGuiTableColumnFlags_WidthFixed,
                    optionsSideMargin);
                ImGui::TableSetupColumn("OptionTitle", ImGuiTableColumnFlags_WidthFixed,
                    optionTitleWidth);
                ImGui::TableSetupColumn("OptionDescription", ImGuiTableColumnFlags_WidthFixed,
                    optionDescriptionWidth);
                ImGui::TableSetupColumn("OptionControls", ImGuiTableColumnFlags_WidthStretch, 1.0F);
                ImGui::TableSetupColumn("OptionRightMargin", ImGuiTableColumnFlags_WidthFixed,
                    optionsSideMargin);

                std::size_t optionGroupIndex{};
                const auto beginOptionRow = [&](const char* title, const char* description,
                                                const float rowHeight) {
                    if (optionGroupIndex++ > 0U) {
                        ImGui::TableNextRow(ImGuiTableRowFlags_None, optionSeparatorHeight);
                        ImGui::TableSetColumnIndex(1);
                        ImVec2 separatorOrigin{};
                        ImGui::GetCursorScreenPos(&separatorOrigin);
                        const float separatorY = separatorOrigin.y + optionSeparatorHeight * 0.5F;
                        ImGui::ImDrawListManager::AddLine(ImGui::GetWindowDrawList(),
                            ImVec2(separatorOrigin.x, separatorY),
                            ImVec2(separatorOrigin.x + (std::max)(80.0F,
                                optionsInnerWidth - 40.0F * g_uiScale), separatorY),
                            ImGui::GetColorU32(ImVec4(0.18F, 0.19F, 0.21F, 0.90F)),
                            (std::max)(2.0F, 2.25F * g_uiScale));
                    }
                    ImGui::TableNextRow(ImGuiTableRowFlags_None, rowHeight);
                    ImGui::TableSetColumnIndex(1);
                    RenderScaledBoldLine(title, 1.28F, ImVec4(0.98F, 0.99F, 1.0F, 1.0F));
                    ImGui::TableSetColumnIndex(2);
                    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.90F, 0.93F, 0.97F, 1.0F));
                    ImGui::TextWrapped("%s", description);
                    ImGui::PopStyleColor();
                    ImGui::TableSetColumnIndex(3);
                };

                beginOptionRow(UiText("Hotkey scanner", "단축키 스캐너", "快捷键扫描器"),
                    UiText("Scan the active virtual Data environment in four ordered stages. Unchanged files are reused from the cache.",
                        "활성 가상 Data 환경을 네 단계로 순서대로 읽으며, 변경되지 않은 파일은 캐시를 재사용합니다.",
                        "按四个阶段扫描当前虚拟 Data 环境，未更改的文件会复用缓存。"),
                    scannerGroupHeight);

                const char* scannerStages[] = {
                    UiText("1  Configuration & archives", "1  설정·구성·아카이브", "1  配置、设置与归档"),
                    UiText("2  Papyrus scripts & PEX", "2  Papyrus 스크립트·PEX", "2  Papyrus 脚本与 PEX"),
                    UiText("3  Native plugins & DLL", "3  네이티브 플러그인·DLL", "3  原生插件与 DLL"),
                    UiText("4  Saved registrations & tools", "4  저장된 등록·외부 도구", "4  已保存注册与外部工具")
                };
                const char* stageKeys[] = {
                    "Configuration & archives", "Scripts", "Native plugins", "Saved & runtime registrations"
                };
                // This table is already inside the padded controls cell of the
                // outer options table.  Reusing the outer vertical cell padding
                // here would inset the stage and action columns a second time,
                // leaving them visibly lower than the scanner title and
                // description.  Keep only a small horizontal gutter so every
                // scanner column shares the same top edge.
                ImGui::PushStyleVar(ImGuiStyleVar_CellPadding,
                    ImVec2(8.0F * g_uiScale, 0.0F));
                if (ImGui::BeginTable("ScannerStagesAndControls", 3,
                        ImGuiTableFlags_SizingStretchProp)) {
                    ImGui::TableSetupColumn("ScannerStageOdd", ImGuiTableColumnFlags_WidthStretch, 0.20F);
                    ImGui::TableSetupColumn("ScannerStageEven", ImGuiTableColumnFlags_WidthStretch, 0.20F);
                    ImGui::TableSetupColumn("ScannerActions", ImGuiTableColumnFlags_WidthStretch, 0.60F);
                    ImGui::TableNextRow();

                    const auto renderScannerStage = [&](const std::size_t index) {
                        const bool activeStage = running && scanStage == stageKeys[index];
                        if (activeStage) ImGui::PushStyleColor(ImGuiCol_Text,
                            ImVec4(0.25F, 0.86F, 0.96F, 1.0F));
                        ImGui::TextUnformatted(scannerStages[index]);
                        if (activeStage) ImGui::PopStyleColor();
                    };

                    ImGui::TableSetColumnIndex(0);
                    renderScannerStage(0);
                    ImGui::Spacing();
                    renderScannerStage(2);

                    ImGui::TableSetColumnIndex(1);
                    renderScannerStage(1);
                    ImGui::Spacing();
                    renderScannerStage(3);

                    ImGui::TableSetColumnIndex(2);
                    if (!running) {
                        const char* startLabel = UiText("Start full scan", "전체 스캔 시작", "开始完整扫描");
                        if (ImGui::Button(startLabel, ScaledButtonSize(startLabel, 150.0F, 34.0F)) && startScan) startScan();
                    } else {
                        const char* cancelLabel = UiText("Cancel scan", "스캔 취소", "取消扫描");
                        if (ImGui::Button(cancelLabel, ScaledButtonSize(cancelLabel, 135.0F, 34.0F)) && cancelScan) cancelScan();
                    }
                    ImGui::SameLine();
                    ImGui::Text("%s: %s", UiText("Status", "상태", "状态"),
                        LocalizedScanStatus(scanStage));
                    char progressText[16]{};
                    std::snprintf(progressText, sizeof(progressText), "%.0f%%",
                        std::clamp(scanPercent, 0.0F, 100.0F));
                    const float progressHeight = (std::max)(32.0F * g_uiScale,
                        ImGui::GetTextLineHeightWithSpacing() + 8.0F * g_uiScale);
                    ImGui::ProgressBar(std::clamp(scanPercent / 100.0F, 0.0F, 1.0F),
                        ImVec2(-1.0F, progressHeight), progressText);
                    if (running && !scanPath.empty()) {
                        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.90F, 0.93F, 0.97F, 1.0F));
                        std::string displayPath = scanPath;
                        displayPath += " (";
                        char fileProgressText[16]{};
                        std::snprintf(fileProgressText, sizeof(fileProgressText), "%.0f%%",
                            std::clamp(scanFilePercent, 0.0F, 100.0F));
                        displayPath += fileProgressText;
                        displayPath += ')';
                        ImVec2 pathAvailable{};
                        ImGui::GetContentRegionAvail(&pathAvailable);
                        const float measuredWidth = MeasureText(displayPath.c_str()).x;
                        const float usableWidth = (std::max)(1.0F,
                            pathAvailable.x - 4.0F * g_uiScale);
                        const float fitScale = measuredWidth > usableWidth ?
                            std::clamp(usableWidth / measuredWidth, 0.12F, 1.0F) : 1.0F;
                        ImGui::SetWindowFontScale(g_uiScale * fitScale);
                        ImGui::TextUnformatted(displayPath.c_str());
                        ImGui::SetWindowFontScale(g_uiScale);
                        ImGui::PopStyleColor();
                    }
                    ImGui::EndTable();
                }
                ImGui::PopStyleVar();

                beginOptionRow(UiText("Appearance", "화면 표시", "显示"),
                    UiText("Adjust overall text size, window transparency, and automatic or fixed interface language.",
                        "전체 글자 크기와 UI 창 투명도를 조절하고 인터페이스 언어를 자동 또는 수동으로 선택합니다.",
                        "调整整体文字大小和界面窗口透明度，并自动或手动选择界面语言。"),
                    appearanceGroupHeight);
                float textSizePercent = g_uiScale * 100.0F;
                const char* textSizeLabel = UiText("Text size (range 80% to 135%)",
                    "글자 크기 (범위 80%~135%)", "文字大小（范围 80% 至 135%）");
                const float resetWidth = ScaledButtonSize(UiText("Reset", "초기화", "重置"), 92.0F, 34.0F).x;
                ImVec2 appearanceAvailable{};
                ImGui::GetContentRegionAvail(&appearanceAvailable);
                const float textSizeLabelWidth = (std::max)(260.0F * g_uiScale,
                    MeasureText(textSizeLabel).x + 20.0F * g_uiScale);
                ImGui::AlignTextToFramePadding();
                ImGui::TextUnformatted(textSizeLabel);
                ImGui::SameLine(textSizeLabelWidth);
                ImGui::SetNextItemWidth((std::max)(240.0F * g_uiScale,
                    appearanceAvailable.x - textSizeLabelWidth - resetWidth - 18.0F * g_uiScale));
                if (ImGui::SliderFloat("##OverallTextSize",
                        &textSizePercent, 80.0F, 135.0F, "%.0f%%")) {
                    std::scoped_lock lock(g_preferencesMutex);
                    g_uiScale = std::clamp(textSizePercent / 100.0F, 0.80F, 1.35F);
                    g_openingHotkeyDraft.uiScale = g_uiScale;
                    g_preferencesDirty = true;
                }
                ImGui::SameLine();
                const char* resetLabel = UiText("Reset", "초기화", "重置");
                if (ImGui::Button(resetLabel, ScaledButtonSize(resetLabel, 92.0F, 34.0F))) {
                    std::scoped_lock lock(g_preferencesMutex);
                    g_uiScale = 1.0F;
                    g_openingHotkeyDraft.uiScale = 1.0F;
                    g_preferencesDirty = true;
                }
                ImGui::AlignTextToFramePadding();
                ImGui::TextUnformatted(UiText("Interface language", "인터페이스 언어", "界面语言"));
                ImGui::SameLine(textSizeLabelWidth);
                const char* languageItems[]{ "Automatic (Windows)", "한국어", "English", "中文" };
                constexpr UHI::UiLanguage languageValues[]{ UHI::UiLanguage::automatic,
                    UHI::UiLanguage::korean, UHI::UiLanguage::english, UHI::UiLanguage::chinese };
                int languageIndex = 0;
                const auto currentLanguage = g_uiLanguage.load();
                for (int index = 0; index < static_cast<int>(std::size(languageValues)); ++index) {
                    if (languageValues[index] == currentLanguage) languageIndex = index;
                }
                ImGui::SetNextItemWidth(300.0F * g_uiScale);
                if (ImGui::Combo("##InterfaceLanguage", &languageIndex, languageItems,
                        static_cast<int>(std::size(languageItems)))) {
                    std::scoped_lock lock(g_preferencesMutex);
                    const auto selected = languageValues[languageIndex];
                    g_uiLanguage.store(selected);
                    g_openingHotkeyDraft.uiLanguage = selected;
                    g_preferencesDirty = true;
                }
                float opacityPercent = g_windowOpacity * 100.0F;
                const char* opacityLabel = UiText("UI window opacity (range 35% to 100%)",
                    "UI 창 투명도 (범위 35%~100%)", "界面窗口不透明度（范围 35% 至 100%）");
                ImGui::AlignTextToFramePadding();
                ImGui::TextUnformatted(opacityLabel);
                ImGui::SameLine(textSizeLabelWidth);
                ImGui::SetNextItemWidth((std::max)(240.0F * g_uiScale,
                    appearanceAvailable.x - textSizeLabelWidth - resetWidth - 18.0F * g_uiScale));
                if (ImGui::SliderFloat("##WindowOpacity", &opacityPercent,
                        35.0F, 100.0F, "%.0f%%")) {
                    std::scoped_lock lock(g_preferencesMutex);
                    g_windowOpacity = std::clamp(opacityPercent / 100.0F, 0.35F, 1.0F);
                    g_openingHotkeyDraft.windowOpacity = g_windowOpacity;
                    g_preferencesDirty = true;
                }

                beginOptionRow(UiText("Opening shortcut", "창 열기 단축키", "窗口快捷键"),
                    UiText("Choose the exact keyboard shortcut that opens and closes the manager.",
                        "관리 창을 열고 닫는 정확한 키보드 단축키를 지정합니다.",
                        "选择用于打开和关闭管理窗口的精确键盘快捷键。"),
                    optionGroupHeight);
                ImVec2 shortcutAvailable{};
                ImGui::GetContentRegionAvail(&shortcutAvailable);
                const bool capturing = UHI::IsMenuFrameworkOpeningHotkeyCaptureActive();
                const std::string captureLabel = capturing ?
                    UiText("Press a keyboard shortcut...", "변경할 단축키를 누르세요...", "请按下新的键盘快捷键...") :
                    UHI::FormatOpeningHotkey(preferencesDraft);
                if (capturing) {
                    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.12F, 0.43F, 0.53F, 1.0F));
                    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.14F, 0.50F, 0.60F, 1.0F));
                }
                const float captureWidth = (std::min)(560.0F * g_uiScale,
                    shortcutAvailable.x - 140.0F * g_uiScale);
                if (ImGui::Button(captureLabel.c_str(), ImVec2(captureWidth, 38.0F * g_uiScale))) {
                    if (UHI::BeginMenuFrameworkOpeningHotkeyCapture())
                        g_openingHotkeyCaptureArmedFrame = ImGui::GetFrameCount();
                }
                if (capturing) ImGui::PopStyleColor(2);
                if (UHI::IsMenuFrameworkOpeningHotkeyCaptureActive())
                    PollImGuiOpeningHotkeyCapture();
                ImGui::SameLine();
                const char* defaultLabel = UiText("Default Del", "기본 Del", "默认 Del");
                if (ImGui::Button(defaultLabel, ScaledButtonSize(defaultLabel, 110.0F, 32.0F))) {
                    UHI::CancelMenuFrameworkOpeningHotkeyCapture();
                    std::scoped_lock lock(g_preferencesMutex);
                    const float retainedScale = g_uiScale;
                    const float retainedOpacity = g_windowOpacity;
                    const auto retainedLanguage = g_uiLanguage.load();
                    g_openingHotkeyDraft = {};
                    g_openingHotkeyDraft.uiScale = retainedScale;
                    g_openingHotkeyDraft.windowOpacity = retainedOpacity;
                    g_openingHotkeyDraft.uiLanguage = retainedLanguage;
                    preferencesDraft = g_openingHotkeyDraft;
                    g_preferencesDirty = true;
                }
                ImGui::TextDisabled("%s", UiText(
                    "Click the field, then press the desired key or modifier chord. Escape cancels capture. Changes save when the window closes.",
                    "필드를 누른 뒤 원하는 키나 모디파이어 조합을 입력하세요. ESC는 입력을 취소하며, 창을 닫으면 자동 저장됩니다.",
                    "点击输入框后按下所需按键或修饰键组合。ESC 可取消输入，关闭窗口时自动保存。"));

                ImGui::EndTable();
            }
            ImGui::PopStyleVar();
        }
        if (g_page == HotkeyViewPage::category) {
            const auto categoryName = std::string(CategoryLabel(g_category));
            std::size_t categoryCount{};
            std::size_t categoryEditable{};
            std::size_t categoryReadOnly{};
            std::unordered_set<std::string> categoryConflictBindings;
            if (snapshot) {
                for (const auto& group : snapshot->groups) {
                    for (const auto& item : group.entries) {
                        if (item.category != g_category) continue;
                        ++categoryCount;
                        if (item.conflict) {
                            categoryConflictBindings.insert(group.device + "\x1F" +
                                UHI::NormalizeBinding(item.record->binding));
                        }
                        categoryEditable += item.record->editable ? 1U : 0U;
                        categoryReadOnly += item.record->editable ? 0U : 1U;
                    }
                }
            }
            const auto categoryConflicts = categoryConflictBindings.size();
            const char* categoryBindingLabel = UiText("BINDINGS", "단축키", "快捷键");
            const char* categoryConflictLabel = UiText("CONFLICTS", "충돌", "冲突");
            const char* categoryEditableLabel = UiText("EDITABLE", "변경 가능", "可修改");
            const char* categoryReadOnlyLabel = UiText("READ-ONLY", "읽기 전용", "只读");
            ImVec2 categoryHeaderAvailable{};
            ImGui::GetContentRegionAvail(&categoryHeaderAvailable);
            const float categoryHeaderMargin = MeasureText(UiText("M", "가", "字")).x;
            const float categoryHeaderUsableWidth = (std::max)(0.0F,
                categoryHeaderAvailable.x - categoryHeaderMargin * 2.0F);
            const float categorySummaryWidth = (std::min)(620.0F * g_uiScale,
                categoryHeaderUsableWidth * 0.52F);
            ImGui::Spacing();
            if (ImGui::BeginTable("CategoryHeader", 4,
                    ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_NoPadOuterX)) {
                ImGui::TableSetupColumn("LeftMargin", ImGuiTableColumnFlags_WidthFixed, categoryHeaderMargin);
                ImGui::TableSetupColumn("Description", ImGuiTableColumnFlags_WidthStretch, 1.0F);
                ImGui::TableSetupColumn("Summary", ImGuiTableColumnFlags_WidthFixed, categorySummaryWidth);
                ImGui::TableSetupColumn("RightMargin", ImGuiTableColumnFlags_WidthFixed, categoryHeaderMargin);
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(1);
                const float headerTop = ImGui::GetCursorPosY();
                RenderScaledBoldLine(categoryName.c_str(), 1.50F, ImVec4(0.98F, 0.99F, 1.0F, 1.0F));
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.94F, 0.96F, 0.99F, 1.0F));
                ImGui::TextWrapped("%s", CategoryDescription(g_category));
                ImGui::PopStyleColor();

                ImGui::TableSetColumnIndex(2);
                ImGui::SetCursorPosY(headerTop);
                if (ImGui::BeginTable("CategorySummaryGrid", 4,
                        ImGuiTableFlags_SizingStretchSame | ImGuiTableFlags_NoPadOuterX)) {
                    constexpr std::array<std::size_t, 4> metricIndices{ 0, 1, 2, 3 };
                    const std::array<const char*, 4> metricLabels{
                        categoryBindingLabel, categoryConflictLabel, categoryEditableLabel, categoryReadOnlyLabel
                    };
                    const std::array<std::size_t, 4> metricValues{
                        categoryCount, categoryConflicts, categoryEditable, categoryReadOnly
                    };
                    const std::array<ImVec4, 4> metricColors{
                        ImVec4(0.90F, 0.93F, 0.98F, 1.0F), ImVec4(1.0F, 0.42F, 0.36F, 1.0F),
                        ImVec4(0.35F, 0.85F, 0.65F, 1.0F), ImVec4(0.86F, 0.89F, 0.94F, 1.0F)
                    };
                    ImGui::TableNextRow();
                    for (const auto index : metricIndices) {
                        ImGui::TableSetColumnIndex(static_cast<int>(index));
                        ImVec2 cellAvailable{};
                        ImGui::GetContentRegionAvail(&cellAvailable);
                        ImGui::SetCursorPosX(ImGui::GetCursorPosX() +
                            (std::max)(0.0F, cellAvailable.x - MeasureText(metricLabels[index]).x));
                        ImGui::TextUnformatted(metricLabels[index]);
                    }
                    ImGui::TableNextRow();
                    ImGui::SetWindowFontScale(g_uiScale * 1.50F);
                    for (const auto index : metricIndices) {
                        ImGui::TableSetColumnIndex(static_cast<int>(index));
                        const auto number = std::to_string(metricValues[index]);
                        ImVec2 cellAvailable{};
                        ImGui::GetContentRegionAvail(&cellAvailable);
                        ImGui::SetCursorPosX(ImGui::GetCursorPosX() +
                            (std::max)(0.0F, cellAvailable.x - MeasureText(number.c_str()).x));
                        ImGui::TextColored(metricColors[index], "%s", number.c_str());
                    }
                    ImGui::SetWindowFontScale(g_uiScale);
                    ImGui::EndTable();
                }
                ImGui::EndTable();
            }
            ImGui::Spacing();
            RenderFilterToolbar();
            UpdateFilteredGroups(snapshot, g_category);
            if (registry && registry->WasTruncated()) {
                ImGui::TextColored(ImVec4(1.0F, 0.70F, 0.25F, 1.0F),
                    UiText("Safety limit reached: %zu additional findings were omitted.",
                        "안전 제한에 도달해 추가 결과 %zu개를 생략했습니다.",
                        "已达到安全限制，省略了另外 %zu 个结果。"), registry->DroppedCount());
            }
            const bool contentVisible = ImGui::BeginChild("CategoryContent", ImVec2(0.0F, 0.0F), false);
            if (contentVisible) {
                if (g_categoryMapView) {
                    RenderVectorOverview(g_filteredGroups);
                } else {
                    RenderBindingTable(g_filteredGroups);
                }
            }
            ImGui::EndChild();
        }
        if (g_page == HotkeyViewPage::overview && g_categoryMapView) {
            UHI::OpeningHotkey badgeHotkey;
            {
                std::scoped_lock lock(g_preferencesMutex);
                if (g_openingHotkeyDraftLoaded) badgeHotkey = g_openingHotkeyDraft;
            }
            RenderOverviewShortcutBadge(badgeHotkey);
        }
        RenderEditorPopups();
        // Menu Framework can own Escape before Skyrim's input sink sees it.
        // Close the parent here as a reliable fallback, but only when no popup
        // or capture owns this physical press. Popup cancellation sets the
        // suppression latch, preventing one Escape from closing two layers.
        if (ImGui::IsKeyPressed(ImGuiKey_Escape, false) &&
            !g_escapeConsumedByPopupThisFrame &&
            !g_modalInputActive.load() &&
            !g_openingHotkeyCaptureActive.load() &&
            !ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId) &&
            !UHI::IsMenuFrameworkEscapeCloseSuppressed()) {
            UHI::CloseMenuFrameworkWindow();
            open = false;
        }
        ImGui::End();
        ImGui::PopStyleColor(12);
        ImGui::PopStyleVar(9);
    }

    void __stdcall RenderFrameworkEntry()
    {
        if (!HasMenuFrameworkContext()) {
            return;
        }
        ImGui::TextUnformatted("Universal Hotkey Manager for Skyrim SE-AE");
        ImGui::TextWrapped("%s", UiText("Open the manager here if its configured keyboard shortcut is unavailable.",
            "설정한 키보드 단축키를 사용할 수 없을 때 여기서 관리 창을 여세요.",
            "如果设定的键盘快捷键不可用，请在此打开管理窗口。"));
        if (ImGui::Button(UiText("Universal Hotkey Manager", "Universal Hotkey Manager", "Universal Hotkey Manager")) && g_window) {
            g_automaticRefreshRequestedForCurrentOpen = false;
            g_renderWindowVisible = true;
            g_focusWindowOnNextRender = true;
            g_window->IsOpen = true;
        }
    }
}

namespace UHI
{
    bool RegisterMenuFrameworkWindow()
    {
        if (!g_window) {
            // UHI no longer imports the framework DLL. Refresh the official
            // header's module handle after SKSE has loaded every plugin.
            menuFramework = GetModuleHandleW(L"SKSEMenuFramework.dll");
            if (!menuFramework) {
                SKSE::log::error("SKSE Menu Framework is not loaded; UHM window registration skipped");
                return false;
            }
            SKSEMenuFramework::SetSection("Universal Hotkey Manager for Skyrim SE-AE");
            g_window = SKSEMenuFramework::AddWindow(RenderHotkeyView);
            SKSEMenuFramework::AddSectionItem("Universal Hotkey Manager for Skyrim SE-AE", RenderFrameworkEntry);
            // Delete is the default UHM toggle key. The framework owns focus and
            // input routing once the window is open; no game launch is needed
            // during development to register this render callback.
        }
        return g_window != nullptr;
    }

    void SetMenuFrameworkRegistry(std::shared_ptr<const Registry> registry, const bool restored)
    {
        auto snapshot = std::make_shared<RegistrySnapshot>();
        snapshot->registry = std::move(registry);
        snapshot->restored = restored;
        if (snapshot->registry) {
            snapshot->groups = BuildHotkeyView(*snapshot->registry);
            snapshot->conflictCount = snapshot->registry->Conflicts().size();
            snapshot->conditionalConflictCount = snapshot->registry->ConditionalConflictCount();
        }
        std::scoped_lock lock(g_registryMutex);
        g_registrySnapshot = std::move(snapshot);
    }

    void SetMenuFrameworkSexLabInstalled(const bool installed) noexcept
    {
        g_sexLabInstalled.store(installed);
    }

    bool ToggleMenuFrameworkWindow()
    {
        if (g_window) {
            const bool wasOpen = g_window->IsOpen.load() || g_renderWindowVisible.load();
            if (wasOpen) {
                g_openingHotkeyCaptureActive = false;
                g_visibleChangedHotkeyNotice = 0U;
                CloseEditorModal();
                SavePendingPreferences();
            } else {
                g_automaticRefreshRequestedForCurrentOpen = false;
                g_focusWindowOnNextRender = true;
            }
            g_window->IsOpen = !wasOpen;
            g_renderWindowVisible = !wasOpen;
            if (wasOpen) g_focusWindowOnNextRender = false;
            return true;
        }
        return false;
    }

    bool CloseMenuFrameworkWindow()
    {
        if (!g_window || (!g_window->IsOpen.load() && !g_renderWindowVisible.load())) return false;
        g_renderWindowVisible = false;
        g_focusWindowOnNextRender = false;
        g_openingHotkeyCaptureActive = false;
        g_visibleChangedHotkeyNotice = 0U;
        CloseEditorModal();
        SavePendingPreferences();
        g_window->IsOpen = false;
        g_automaticRefreshRequestedForCurrentOpen = false;
        return true;
    }

    bool IsMenuFrameworkWindowOpen() noexcept
    {
        // Some Menu Framework builds update WindowInterface::IsOpen one frame
        // after invoking the render callback. The render-visible latch makes
        // Escape handling authoritative during that frame and is cleared by
        // every UHM close path.
        return g_window && (g_window->IsOpen.load() || g_renderWindowVisible.load());
    }

    bool BeginMenuFrameworkOpeningHotkeyCapture() noexcept
    {
        if (!IsMenuFrameworkWindowOpen()) return false;
        std::scoped_lock lock(g_preferencesMutex);
        if (!g_openingHotkeyDraftLoaded) return false;
        g_openingHotkeyCaptureActive = true;
        return true;
    }

    bool IsMenuFrameworkOpeningHotkeyCaptureActive() noexcept
    {
        return g_openingHotkeyCaptureActive.load();
    }

    bool CaptureMenuFrameworkOpeningHotkey(const std::uint32_t scanCode,
        const std::uint32_t ctrlScanCode, const std::uint32_t shiftScanCode,
        const std::uint32_t altScanCode) noexcept
    {
        if (!g_openingHotkeyCaptureActive.load()) return false;
        std::scoped_lock lock(g_preferencesMutex);
        if (!g_openingHotkeyDraftLoaded) return false;
        OpeningHotkey captured{
            .scanCode = scanCode,
            .ctrl = ctrlScanCode != 0,
            .shift = shiftScanCode != 0,
            .alt = altScanCode != 0,
            .ctrlScanCode = ctrlScanCode,
            .shiftScanCode = shiftScanCode,
            .altScanCode = altScanCode,
            .uiScale = g_uiScale,
            .windowOpacity = g_windowOpacity,
            .uiLanguage = g_uiLanguage.load()
        };
        if (!IsValidOpeningHotkey(captured)) return false;
        g_openingHotkeyDraft = captured;
        g_preferencesDirty = true;
        g_openingHotkeyCaptureActive = false;
        return true;
    }

    void CancelMenuFrameworkOpeningHotkeyCapture() noexcept
    {
        // This function is also reached from the Skyrim input sink when Escape
        // cancels capture. Keep that physical press from closing the parent.
        SuppressWindowCloseForEscapePress();
        g_openingHotkeyCaptureActive = false;
    }

    bool IsMenuFrameworkModalInputActive() noexcept
    {
        return g_modalInputActive.load();
    }

    bool IsMenuFrameworkEscapeCloseSuppressed() noexcept
    {
        const auto deadline = g_suppressWindowCloseUntilMilliseconds.load();
        if (deadline <= 0) return false;
        if (MonotonicMilliseconds() >= deadline) {
            // Some input stacks do not deliver the Escape release after the
            // down event was deliberately stopped.  Expire the popup-only
            // guard so a later, separate Escape can always close UHM.
            g_suppressWindowCloseUntilEscapeRelease = false;
            g_suppressWindowCloseUntilMilliseconds = 0;
            return false;
        }
        return g_suppressWindowCloseUntilEscapeRelease.load();
    }

    void ReleaseMenuFrameworkEscapeCloseSuppression() noexcept
    {
        g_suppressWindowCloseUntilEscapeRelease = false;
        g_suppressWindowCloseUntilMilliseconds = 0;
    }

    bool IsMenuFrameworkBindingCaptureActive() noexcept
    {
        return g_bindingCaptureActive.load();
    }

    bool CaptureMenuFrameworkBindingInput(const std::string_view device,
        const std::uint32_t mainCode, const std::string_view modifierDevice,
        const std::uint32_t modifierCode) noexcept
    {
        try {
            std::scoped_lock lock(g_editorMutex);
            if (!g_bindingCaptureActive.load() || g_editorModal != EditorModal::binding || !g_editorRecord)
                return false;
            const auto serialized = UHI::SerializeCapturedBinding(*g_editorRecord,
                device, mainCode, modifierDevice, modifierCode);
            if (!serialized.error.empty()) {
                g_bindingCaptureStatus = serialized.error;
                g_bindingCaptureDisplay.clear();
                g_bindingCaptureRaw.clear();
                g_bindingCaptureActive = false;
                return false;
            }
            // Preserve serialized.raw exactly; normalize only the visible
            // capture label to the same names used by the device diagram.
            g_bindingCaptureDisplay = FriendlyBindingLabel(serialized.display);
            g_bindingCaptureRaw = serialized.raw;
            g_bindingCaptureStatus.clear();
            g_bindingCaptureActive = false;
            return true;
        } catch (...) {
            g_bindingCaptureActive = false;
            return false;
        }
    }

    void CancelMenuFrameworkBindingCapture() noexcept
    {
        SuppressWindowCloseForEscapePress();
        g_bindingCaptureActive = false;
    }

    void CancelMenuFrameworkEditorModal() noexcept
    {
        // The input sink may run outside the ImGui render path. Request a
        // render-thread close instead of mutating popup state concurrently.
        g_cancelActivePopupRequested = true;
        SuppressWindowCloseForEscapePress();
        g_bindingCaptureActive = false;
    }

    void SetMenuFrameworkScanStatus(const bool running, const float percent, const float currentFilePercent,
        const std::string_view stage, const std::string_view path)
    {
        std::scoped_lock lock(g_statusMutex);
        g_scanRunning = running;
        g_scanPercent = percent;
        g_scanFilePercent = currentFilePercent;
        g_scanStage.assign(stage);
        g_scanPath.assign(path);
    }

    void SetMenuFrameworkChangedHotkeyNotice(const std::size_t count) noexcept
    {
        if (count > 0U) g_pendingChangedHotkeyNotice.store(count);
    }

    void SetMenuFrameworkFontRestartRequired() noexcept
    {
        g_pendingBindingWriteNotice.store(
            static_cast<int>(BindingWriteNotice::fontRestartRequired));
    }

    void SetMenuFrameworkStartScan(std::function<void()> startScan)
    {
        std::scoped_lock lock(g_statusMutex);
        g_startScan = std::move(startScan);
    }

    void SetMenuFrameworkAutomaticRefresh(std::function<void()> refresh)
    {
        std::scoped_lock lock(g_statusMutex);
        g_automaticRefresh = std::move(refresh);
    }

    void SetMenuFrameworkCancelScan(std::function<void()> cancelScan)
    {
        std::scoped_lock lock(g_statusMutex);
        g_cancelScan = std::move(cancelScan);
    }

    void SetMenuFrameworkOpeningHotkey(
        std::function<OpeningHotkey()> getHotkey,
        std::function<bool(const OpeningHotkey&)> saveHotkey)
    {
        std::scoped_lock lock(g_statusMutex, g_preferencesMutex);
        g_getOpeningHotkey = std::move(getHotkey);
        g_saveOpeningHotkey = std::move(saveHotkey);
        g_openingHotkeyDraftLoaded = false;
        g_preferencesDirty = false;
    }

    void SetMenuFrameworkBindingWriter(BindingWriter writer)
    {
        std::scoped_lock lock(g_statusMutex);
        g_bindingWriter = std::move(writer);
    }
}
#else
namespace UHI
{
    bool RegisterMenuFrameworkWindow() { return false; }
    void SetMenuFrameworkRegistry(std::shared_ptr<const Registry>, bool) {}
    void SetMenuFrameworkChangedHotkeyNotice(std::size_t) noexcept {}
    void SetMenuFrameworkFontRestartRequired() noexcept {}
    void SetMenuFrameworkSexLabInstalled(bool) noexcept {}
    bool ToggleMenuFrameworkWindow() { return false; }
    bool CloseMenuFrameworkWindow() { return false; }
    bool IsMenuFrameworkWindowOpen() noexcept { return false; }
    bool BeginMenuFrameworkOpeningHotkeyCapture() noexcept { return false; }
    bool IsMenuFrameworkOpeningHotkeyCaptureActive() noexcept { return false; }
    bool CaptureMenuFrameworkOpeningHotkey(std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t) noexcept { return false; }
    void CancelMenuFrameworkOpeningHotkeyCapture() noexcept {}
    bool IsMenuFrameworkModalInputActive() noexcept { return false; }
    bool IsMenuFrameworkEscapeCloseSuppressed() noexcept { return false; }
    void ReleaseMenuFrameworkEscapeCloseSuppression() noexcept {}
    bool IsMenuFrameworkBindingCaptureActive() noexcept { return false; }
    bool CaptureMenuFrameworkBindingInput(std::string_view, std::uint32_t,
        std::string_view, std::uint32_t) noexcept { return false; }
    void CancelMenuFrameworkBindingCapture() noexcept {}
    void CancelMenuFrameworkEditorModal() noexcept {}
    void SetMenuFrameworkScanStatus(bool, float, float, std::string_view, std::string_view) {}
    void SetMenuFrameworkStartScan(std::function<void()>) {}
    void SetMenuFrameworkAutomaticRefresh(std::function<void()>) {}
    void SetMenuFrameworkCancelScan(std::function<void()>) {}
    void SetMenuFrameworkOpeningHotkey(
        std::function<OpeningHotkey()>, std::function<bool(const OpeningHotkey&)>) {}
    void SetMenuFrameworkBindingWriter(BindingWriter) {}
}
#endif
