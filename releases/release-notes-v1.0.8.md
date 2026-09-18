# Universal Hotkey Manager 1.0.8

Fixes a menu-close state bug that could leave UHM consuming the mouse wheel after its window disappeared, preventing character camera zoom until a save reload.

- Use one menu visibility state; an extra render frame during closing cannot leave UHM marked as open.
- Clean up input capture, popups, queued mouse input and cursor ownership on normal close, forced close and menu destruction.
- Cancel stale queued menu/cursor work and retain the cursor when another menu needs it.
- Use the same close path for the close button, Escape, opening shortcut and external menu API.

Clean SE/AE build and **25/25 automated tests passed**, including delayed/forced closure and repeated open/close sequences. In-game confirmation of the reported camera-zoom symptom is still needed.

Exit Skyrim completely before replacing the mod. Keep your existing settings and binding history. Supported runtimes remain **SE 1.5.97** and **AE 1.6.317 / 318 / 323 / 342 / 353 / 629 / 640 / 659 / 678 / 1130 / 1170 / 1179**. **Skyrim 1.7.x and VR are not supported.** Use SKSE64 and Address Library matching your game version.

## 한국어

UHM을 닫아도 내부 열림 상태가 남아 휠 입력을 계속 소비하고, 세이브를 다시 불러오기 전까지 캐릭터 카메라 줌을 막을 수 있는 종료 처리 결함을 수정했습니다.

- 열림 상태를 하나로 통합하고 종료 중 추가 프레임이 상태를 되살리지 못하게 했습니다.
- 일반 닫기·강제 닫기·메뉴 해제 시 입력 캡처, 팝업, 대기 중인 마우스 입력과 커서 상태를 정리합니다.
- 오래된 메뉴·커서 예약 작업을 취소하며 다른 메뉴가 필요한 커서는 유지합니다.
- 닫기 버튼·ESC·열기 단축키·외부 메뉴 API가 같은 종료 경로를 사용합니다.

SE/AE 클린 빌드와 **자동 테스트 25개를 모두 통과**했습니다. 지연·강제 종료와 반복 개폐를 검증했으며, 제보된 줌 증상의 실제 게임 확인은 필요합니다.

게임을 완전히 종료한 뒤 교체하세요. 기존 설정과 단축키 변경 내역은 유지할 수 있습니다. 지원 버전은 위의 SE 1.5.97 및 명시된 AE 1.6.x와 같으며, **1.7.x와 VR은 미지원**입니다.

[English changelog](https://github.com/compilecraftworks/Universal-Hotkey-Manager-for-Skyrim-SE-AE/blob/v1.0.8/CHANGELOG.md) · [한국어 변경 내역](https://github.com/compilecraftworks/Universal-Hotkey-Manager-for-Skyrim-SE-AE/blob/v1.0.8/CHANGELOG_KO.md)
