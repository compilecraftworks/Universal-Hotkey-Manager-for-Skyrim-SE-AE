# Universal Hotkey Manager 1.1.0

## English

- Fixed scans and hotkey editing becoming unavailable after deleting a save file.
- Fixed editing the wrong JSON setting and missing actions when multiple settings share a name and hotkey. Editing and individual backup restoration now use the exact object/array path, preserving formatting and other settings.
- Fixed stale writes into commented-out settings. Existing supported configuration formats and 1.0.9 binding history are retained.
- MCM changes now distinguish a queued request from verified application. Finished but unsuccessful live changes restore their linked settings file; unfinished requests time out with an explicit notice. Existing direct-property and file-only edits remain available.
- Added the three missing Chinese UI characters while keeping Windows fonts and the existing compact atlas.
- Kept the previous first-open CTD and mouse-wheel fixes. Added regression and allocation-lifetime checks, with release-build Escape assertions restored as active checks.

Exit Skyrim before replacing the files. Run a scan from **Options** once after upgrading. Existing INI settings, `.uhi.bak` files and binding history can be kept. Runtime support is unchanged; Skyrim 1.7.x and VR are not supported.

Validation: clean SE/AE build and all 27 automated tests passed. The Windows font atlas remains 4096 × 2048; 20 font/context cycles retained no allocations beyond the initial character-range cache. Actual Skyrim/Papyrus/mod combinations still require in-game verification.

## 한국어

- 세이브 파일을 삭제한 뒤 스캔과 단축키 편집이 막히던 문제를 수정했습니다.
- 같은 설정명과 키가 있는 JSON에서 다른 항목을 수정하거나 기능이 누락되던 문제를 수정했습니다. 정확한 객체·배열 경로로 편집·개별 백업 복원하며 기존 서식과 다른 설정은 유지합니다.
- 주석 처리된 설정을 잘못 수정하는 문제를 보완했습니다. 기존 지원 설정 형식과 1.0.9의 변경 이력은 유지합니다.
- MCM 변경 요청 대기와 실제 적용 확인을 구분합니다. 완료됐지만 적용되지 않은 변경은 연결 설정 파일을 복구하며, 끝나지 않은 요청은 시간 초과를 알립니다. 기존 속성 직접 변경·파일 저장 기능도 유지합니다.
- 중국어 UI에서 빠진 글자 3개를 추가했습니다. 윈도우 글꼴과 기존 크기의 아틀라스는 유지합니다.
- 기존 첫 열기 CTD·마우스 휠 수정을 유지합니다. 회귀·할당 수명 검사를 추가하고 릴리즈 빌드에서도 ESC 검사문이 실행되도록 보완했습니다.

게임을 완전히 종료한 뒤 파일을 교체하세요. 업데이트 후 **Options에서 스캔을 한 번 실행**하면 됩니다. 기존 INI 설정·`.uhi.bak`·변경 이력은 유지할 수 있습니다. 지원 런타임은 동일하며 Skyrim 1.7.x와 VR은 지원하지 않습니다.

검증: SE/AE 클린 빌드와 자동 테스트 27개를 통과했습니다. 윈도우 글꼴 아틀라스는 4096 × 2048을 유지하며 글꼴/컨텍스트 20회 생성·해제 후 최초 문자 범위 캐시 외 추가 할당이 남지 않았습니다. 실제 Skyrim/Papyrus 및 모드 조합의 인게임 확인은 별도로 필요합니다.
