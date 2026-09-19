# Universal Hotkey Manager 1.1.1

## 1.1.1 - 2026-09-19

- Require matching MCM ownership before writing a live property, global or array element. Accept SkyUI's valid keymap option ID 0 when its page and option type are verified.
- Re-resolve INI/TOML settings by section and key before editing, preserving the expected-value guard after line moves. Refuse duplicate targets within the same section.
- Keep modifier flags and compound key fields within their JSON object, INI/TOML section or YAML parent. Preserve separately stored modifiers in rebind previews; unsupported modifier changes are refused before writing.
- Discover bindings beyond the former 256 KiB prefix and newly added settings during incremental refresh. Refresh live bindings once after loading a save, using unchanged-file caches and waiting for any cancelled scan to finish.
- Replace quadratic conflict-peer storage with shared groups and on-demand peer lists. Keep every conflict entry and peer available, and reuse one analysis when publishing a view.
- Keep UTF-16 configuration bindings visible while correctly marking their unsupported writer as read-only.
- Preserve JSON backup identity after formatting changes and custom action names after rebinding. Migrate 1.1.0 name overrides and retain 1.0.9/1.1.0 binding history.
- Honor UTF-8 BOMs in UHM's own INI and reject non-finite scale/opacity values.
- Honor the last external open/close request when requests arrive before UI dispatch, including close followed immediately by open.
- Fix localized MSVC/Ninja header dependency tracking so incremental builds cannot silently mix old and new data layouts. Add regression and allocation checks for the above paths; retain the existing popup, input-release and Windows font checks.

## 1.1.1 - 2026-09-19

- 실행 중인 MCM 속성·전역 변수·배열을 변경하기 전에 소유 모드를 확인합니다. SkyUI의 페이지와 키맵 유형이 확인된 경우 유효한 첫 옵션 ID 0도 처리합니다.
- INI/TOML 편집 시 섹션과 설정명으로 대상을 다시 찾습니다. 줄이 이동해도 선택한 설정만 변경하고, 기존 값 확인과 같은 섹션의 중복 대상 거부를 유지합니다.
- 보조키 플래그와 복합키 필드를 JSON 객체·INI/TOML 섹션·YAML 부모별로 구분합니다. 별도 필드에 저장된 보조키를 변경 미리보기에 유지하며, 저장할 수 없는 보조키 변경은 쓰기 전에 거부합니다.
- 기존 256KiB 미리 읽기 범위 뒤에 있던 키와 새로 추가된 설정 파일도 감지합니다. 세이브 로드 후 캐시를 활용해 한 번 갱신하고 실행 중인 키 값을 다시 수집하며, 취소 중인 이전 스캔이 끝난 뒤 진행합니다.
- 충돌 상대 목록의 제곱 비례 중복 저장을 공유 그룹과 필요 시 조회 방식으로 바꿨습니다. 모든 충돌 항목과 상대 목록을 유지하고 화면 갱신 시 분석 결과를 재사용합니다.
- UTF-16 설정의 키는 계속 표시하되 지원하지 않는 파일 편집은 읽기 전용으로 정확하게 표시합니다.
- JSON 서식 변경 후에도 백업 대상을 식별하고, 키 재지정 후에도 사용자 지정 기능명을 유지합니다. 1.1.0 이름 설정을 이전하며 1.0.9/1.1.0 변경 이력을 보존합니다.
- UHM 자체 INI의 UTF-8 BOM을 처리하고, 화면 배율·불투명도에 NaN·무한대 값이 들어가지 않도록 수정했습니다.
- 외부 API에서 UI 처리 전에 열기·닫기가 연속 호출되면 마지막 요청을 따릅니다. 닫기 직후 다시 열기가 무시되던 문제를 수정했습니다.
- 한글 MSVC 메시지로 Ninja의 헤더 의존성 추적이 깨져 이전·새 자료 구조가 섞이던 빌드 문제를 수정했습니다. 해당 경로의 회귀·메모리 할당 검사를 추가하고 기존 팝업·입력 해제·윈도우 글꼴 검사를 유지했습니다.

Validation: clean SE/AE build, 31/31 regression tests and 10/10 AddressSanitizer test executables passed. Conflict/view and JSON allocation cycles returned to baseline. In-game validation remains necessary.

검증: SE/AE 클린 빌드, 회귀 테스트 31/31개, AddressSanitizer 검사 실행 파일 10/10개 통과. 충돌 목록·JSON 반복 생성/해제의 추가 잔존 할당은 0바이트입니다. 실제 게임 검증은 별도로 필요합니다.
