# Universal Hotkey Manager 1.0.9

Fixes the crash introduced in 1.0.8 when pressing the hotkey to open UHM for the first time.

- Safely handle an empty popup stack during menu initialization.
- Keep the 1.0.8 mouse-wheel and menu-close fixes.
- Test first opening, stale modal/nested/unsubmitted popups, input cleanup and 100 reopen cycles using the actual ImGui library.

The original access violation was reproduced locally. The clean SE/AE build and **26/26 automated tests passed**. In-game confirmation is still needed.

Exit Skyrim completely before replacing the mod. Existing settings and binding history can be retained. Game-version support and dependencies are unchanged.

## 한국어

1.0.8에서 단축키로 UHM을 처음 열 때 발생하는 CTD를 수정했습니다.

- 메뉴 초기화 시 빈 팝업 목록을 안전하게 처리합니다.
- 1.0.8의 마우스 휠 및 메뉴 종료 수정을 유지합니다.
- 실제 ImGui 라이브러리로 첫 열기, 이전 모달·중첩·아직 그리지 않은 팝업, 입력 정리 및 100회 재열기를 검증했습니다.

기존 접근 위반을 로컬에서 재현했으며, SE/AE 클린 빌드와 **자동 테스트 26개를 모두 통과**했습니다. 실제 게임 확인은 별도로 필요합니다.

게임을 완전히 종료한 뒤 교체하세요. 기존 설정과 단축키 변경 내역은 유지할 수 있습니다. 지원 게임 버전과 의존성은 기존과 같습니다.

[English changelog](https://github.com/compilecraftworks/Universal-Hotkey-Manager-for-Skyrim-SE-AE/blob/v1.0.9/CHANGELOG.md) · [한국어 변경 내역](https://github.com/compilecraftworks/Universal-Hotkey-Manager-for-Skyrim-SE-AE/blob/v1.0.9/CHANGELOG_KO.md)
