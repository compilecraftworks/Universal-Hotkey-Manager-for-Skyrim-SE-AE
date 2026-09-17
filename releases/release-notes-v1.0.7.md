# Universal Hotkey Manager 1.0.7

- Use Windows' current UI font, Malgun Gothic for Korean, and Microsoft YaHei/JhengHei for Chinese. Fonts are loaded from Windows and are not bundled. Optional warm high-contrast text is available in Options.
- Right-click a key to scroll through all its actions. Display the Console launcher and label keys that only operate inside a mod's interface.
- Process every input in a capture batch, retain left/right modifiers, and accept keyboard modifiers with mouse inputs where the source format supports them. Keep the native opening shortcut from consuming input while disabled or unavailable.
- Add Unbind for verified formats, Restore original backup for individual file settings, and a Backups tab for the last 256 successful UHM edits. Refuse stale restores instead of overwriting external changes.
- Improve dedicated/nested JSON and YAML key maps, integral float key codes, and owner-prefixed MCM property matching. These address detection gaps behind the SkyrimNet and OStim reports without hardcoding their key lists.
- Check Community Shaders' actual loaded DLL state, refresh activation after SKSE DLL loading, invalidate older scan snapshots, preserve the Windows hook chain on initialization failure, and reject unverified runtime layouts.

Clean SE/AE-only build and all 24 automated tests passed. The Windows font atlas was 4096 × 2048 on the build machine; required English, Korean and Chinese glyphs were present. The DLL exports the expected SKSE and UHM menu APIs and has no external imgui.dll dependency. Dependencies retain the repository's exact CommonLibSSE-NG 6.7.0 / ImGui 1.91.9 pins and verified transitive closure.

In-game verification is still required for SkyrimNet's generated configuration, OStim's complete runtime key set, and the Risa/Beyond VirtualKey combination. A scalar MCM key code cannot store a chord by itself; UHM reports that limitation and does not invent Beyond VirtualKey IDs. If another mod also uses Delete, select a free key or chord in UHM Options or edit ToggleKey in the INI.

The OAR/Heart of Magic reporter reproduced the issue while UHM was inactive. This build includes defensive input and initialization changes, but does not claim to fix an established UHM cause for that report.

## 한국어

윈도우 기본 UI 글꼴과 한글 맑은 고딕·중문 Microsoft YaHei/JhengHei를 사용합니다. 별도 글꼴 설치나 배포는 없습니다. 옵션에서 따뜻한 고대비 글자를 선택할 수 있습니다.

키 우클릭 전체 목록, Console 이름, 모드 내부 키 표시, 좌우 보조키 및 마우스 조합 캡처, 지원 형식의 단축키 해제, 최초 백업의 개별 값 복원, 최근 256건의 변경 내역 복원을 추가했습니다. 전용 JSON/YAML 키맵과 접두사 MCM 속성 탐지, 비활성 Community Shaders 판별, 시작 시 입력·훅 안전 처리도 보강했습니다.

SE/AE 클린 빌드와 자동 테스트 24개를 통과했습니다. 실제 게임의 SkyrimNet/OStim 전체 탐지 및 Risa/Beyond VirtualKey 조합 검증은 남아 있습니다. 숫자 하나만 저장하는 MCM 형식에는 임의의 조합키나 가상 키 ID를 쓰지 않습니다. OAR/Heart of Magic 제보는 UHM 비활성 상태에서도 재현됐다는 원문 정정을 유지합니다.

[Full English changelog](https://github.com/compilecraftworks/Universal-Hotkey-Manager-for-Skyrim-SE-AE/blob/v1.0.7/CHANGELOG.md) · [전체 한국어 변경 내역](https://github.com/compilecraftworks/Universal-Hotkey-Manager-for-Skyrim-SE-AE/blob/v1.0.7/CHANGELOG_KO.md)
