# Universal Hotkey Manager 1.1.2

## English

- Fix a startup crash introduced in 1.0.7 when the Windows UI font comes from a TrueType Collection (TTC). UHM now reads the complete font collection and selects the same face Windows selected, so internal font offsets remain correct.
- Preserve Windows default fonts, existing Korean/Chinese glyph coverage, font sizes and the compact atlas. No Windows language or font changes are required.
- Validate font directories and verify selected-face identity, repeated font-buffer cleanup and GDI handle cleanup. Add a startup log entry before font-atlas building.
- Reproduced the previous access violation with Microsoft JhengHei UI, MS UI Gothic and Yu Gothic UI. All three pass after the correction. Nine installed Windows faces pass repeated loading tests; the clean SE/AE build and all 31 regression tests pass.

Skyrim AE 1.6.1170 remains supported. This fixes a confirmed font-loading defect; confirmation that it caused an individual user's CTD still requires their logs or an in-game retest. No new in-game validation is claimed.

## 한국어

- Windows UI 글꼴이 TTC 글꼴 모음에 포함된 경우, 1.0.7부터 발생할 수 있던 시작 CTD를 수정했습니다. 글꼴 모음 전체를 읽고 Windows가 선택한 글꼴을 정확히 지정하여 내부 데이터의 기준 위치가 어긋나지 않게 했습니다.
- Windows 기본 글꼴, 기존 한국어·중국어 표시 범위, 글꼴 크기와 아틀라스 구성을 유지합니다. Windows 언어나 글꼴을 변경할 필요가 없습니다.
- 글꼴 디렉터리 검증과 선택된 글꼴의 일치 여부, 반복 로딩 후 메모리·GDI 핸들 정리를 검사했습니다. 글꼴 아틀라스 생성 전 시작 로그를 추가했습니다.
- 기존 접근 위반을 Microsoft JhengHei UI, MS UI Gothic, Yu Gothic UI에서 재현했고 수정 후 모두 통과했습니다. 설치된 Windows 글꼴 9종의 반복 로딩 검사, SE/AE 클린 빌드와 전체 회귀 검사 31개가 통과했습니다.

Skyrim AE 1.6.1170 지원은 유지됩니다. 확인된 글꼴 로딩 결함을 수정한 것이며, 개별 제보자의 CTD와 동일 원인인지 확인하려면 로그 또는 인게임 재검증이 필요합니다. 이번 수정의 실제 게임 검증은 별도입니다.
