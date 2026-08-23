Scriptname UHI_MCM extends SKI_ConfigBase

String Property ScanState = "대기 중" Auto
Int Property ScanPercent = 0 Auto
Bool Property SexLabDetected = False Auto
Function StartScan() native
Float Function GetScanPercent() global native
Bool Function IsScanRunning() global native
Function CancelScan() global native
String Function GetScanStage() global native
String Function GetScanPath() global native
Int Function GetRecordCount() global native
Int Function GetConflictCount() global native

Event OnConfigInit()
    ModName = "Universal Hotkey Manager for Skyrim SE-AE"
    Version = 1
    Pages = new String[8]
    Pages[0] = "단축키 스캔"
    Pages[1] = "전체 / 충돌"
    Pages[2] = "기본 조작"
    Pages[3] = "캐릭터"
    Pages[4] = "전투"
    Pages[5] = "월드 / 환경"
    Pages[6] = "인터페이스"
    Pages[7] = "외부 도구"
    ; The DLL may rebuild this list and append SexLab when detected.
EndEvent

Event OnPageReset(String page)
    If page == "단축키 스캔"
        DrawScanPage()
        Return
    EndIf
    SetCursorFillMode(TOP_TO_BOTTOM)
    AddHeaderOption(page)
    AddTextOption("표시 기준", "기능 목적")
    AddTextOption("필터", "전체 / 충돌 / 후보")
EndEvent

Function DrawScanPage()
    SetCursorFillMode(TOP_TO_BOTTOM)
    AddHeaderOption("Universal Hotkey Manager for Skyrim SE-AE")
    AddTextOption("현재 단계", ScanState)
    AddTextOption("전체 진행률", ScanPercent + "%")
    AddEmptyOption()
    AddTextOption("검사 순서", "설정 → 스크립트 → DLL → 런타임")
    AddTextOption("실행 방식", "백그라운드 스캔")
    AddEmptyOption()
    AddMenuOptionST("START_SCAN", "전체 스캔", "시작")
EndFunction

State START_SCAN
    Event OnSelectST()
        SendModEvent("UHI_StartScan")
        ScanState = "스캔 요청됨"
        ScanPercent = 0
        ForcePageReset()
    EndEvent

    Event OnHighlightST()
        SetInfoText("네 단계를 순서대로 검사합니다. 게임 로딩 중에는 실행되지 않습니다.")
    EndEvent
EndState

Event OnInit()
    RegisterForModEvent("UHI_ScanProgress", "OnUHIProgress")
    RegisterForSingleUpdate(0.4)
EndEvent

Event OnUpdate()
    ScanPercent = GetScanPercent() as Int
    ScanState = GetScanStage()
    If IsScanRunning()
        ScanState = "진행 중: " + ScanState
    ElseIf ScanPercent >= 100
        ScanState = "스캔 완료"
    EndIf
    ForcePageReset()
    RegisterForSingleUpdate(0.4)
EndEvent

Event OnUHIProgress(String eventName, String strArg, Float numArg, Form sender)
    ScanState = strArg
    ScanPercent = numArg as Int
    ForcePageReset()
EndEvent
