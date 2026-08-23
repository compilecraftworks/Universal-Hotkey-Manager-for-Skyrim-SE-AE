# Universal Hotkey Manager for Skyrim SE-AE — Tullius Post (Korean HTML)

```html
<h2>Universal Hotkey Manager for Skyrim SE-AE</h2>

<p><b>모드 단축키, 이제 한 번에 확인하세요.</b></p>

<p>Universal Hotkey Manager for Skyrim SE-AE(UHM)은 스카이림에 설치된 모드들의 단축키를 찾아서 한곳에 정리해 주는 ESP 없는 SKSE 플러그인입니다.</p>

<p>어떤 모드가 어떤 키를 쓰는지, 같은 키가 실제로 충돌하는지, 어디서 설정된 키인지 인게임에서 확인할 수 있습니다. 키보드·마우스·게임패드 바인딩을 지원하며, 스카이림 기본 조작과 MCM, 설정 파일, 스크립트, SKSE 플러그인, ReShade, ENB, Community Shaders 등에서 지원되는 단축키 정보를 수집합니다.</p>

<p style="text-align:center"><img src="https://ac-o.arca.live/20260822sac/df718d47d60f684654b3b2203b13f1dd3b8afc95784a7ddf2eaf6cde960d5e00.png?expires=1787432465&amp;key=6McVuGub4MVjzdSd5oInAQ&amp;type=orig" alt="UHM 오버뷰 캡처"><br><small>오버뷰와 장치 맵</small></p>

<h3>핵심 기능</h3>

<ul>
  <li><b>스캔</b> — 현재 설치된 구성에서 단축키를 찾아냅니다.</li>
  <li><b>감지</b> — 바인딩의 출처와 추정 소유 모드, 활성화 상황을 표시합니다.</li>
  <li><b>관리</b> — 키보드·마우스·게임패드 맵과 목록에서 단축키를 한눈에 확인합니다.</li>
  <li><b>충돌 분석</b> — 양쪽 활성 컨텍스트가 모두 확정되고 동시에 작동 가능한 경우만 충돌로 표시합니다. 불명·추정 컨텍스트가 포함되면 주황색 중복으로 분리합니다.</li>
  <li><b>지원 설정 편집</b> — 지원되는 활성 설정 파일의 바인딩은 관리 창에서 수정할 수 있습니다. DirectInput, SKSE 통합 코드, Windows VK, XInput/controlmap, ReShade, Community Shaders 및 지원 기호 형식을 원본 규칙에 맞춰 저장하고, 손실 가능성이 있는 변환은 거부합니다. 최초 수정 시 <code>.uhi.bak</code> 백업을 만들고, 안전하게 파일을 교체한 뒤 재스캔합니다.</li>
  <li><b>캐시 기반 증분 갱신</b> — 최초 전체 스캔 후에는 완료된 세이브 로드 때만 빠른 증분 확인을 한 번 수행합니다. 창을 열기만 해서는 재스캔하지 않으며 변경되지 않은 파일은 캐시에서 재사용합니다.</li>
  <li><b>세이브와 독립된 결과 저장</b> — 성공한 스캔 결과는 Skyrim 세이브 없이 즉시 저장됩니다. 새로 추가되거나 변경된 활성 단축키가 있을 때만 무딤 없는 개수 알림을 표시합니다.</li>
</ul>

<h3>사용 방법</h3>

<ol>
  <li>기본값인 <b>Delete</b> 키로 관리 창을 엽니다.</li>
  <li><b>Options</b> 탭에서 스캔을 시작합니다.</li>
  <li>카테고리, 기기 맵, 목록, 필터를 사용해 원하는 단축키를 찾습니다.</li>
  <li>Options에서 창을 여는 단축키, 전체 글자 크기, 인터페이스 언어, UI 창 투명도를 변경할 수 있습니다.</li>
  <li>장치 키 좌클릭은 명칭 변경, 우클릭은 명칭/단축키 변경이며 ESC는 편집 내용을 저장하지 않고 팝업을 닫습니다.</li>
</ol>

<p style="text-align:center"><img src="https://ac-o.arca.live/20260822sac/b06518f9e412cd10d96cc3c64046137e7eaa2f2875dc9715a39af80fbad7f8dd.png?expires=1787432465&amp;key=PpQNbJMfVYO2b3w1SjcywQ&amp;type=orig" alt="UHM 단축키 관리 캡처"><br><small>정렬·검색·필터와 단축키 관리 목록</small></p>

<p style="text-align:center"><img src="https://ac-o.arca.live/20260822sac/a7cb6c6ae6070b89a70f12a35a133188d703f0ded837994284099c38ddaf814b.png?expires=1787432465&amp;key=mQIIuEmXwguUsBRoc7kfQw&amp;type=orig" alt="UHM 옵션 캡처"><br><small>스캐너·글자 크기·언어·투명도·창 열기 단축키 설정</small></p>

<h3>열기 키와 캐시 복구</h3>
<p>열기 키 설정: <code>Data/SKSE/Plugins/UniversalHotkeyManager.ini</code><br>MO2: <code>Overwrite/SKSE/Plugins/UniversalHotkeyManager.ini</code><br><code>ToggleKey=0xD3</code>는 Delete입니다.</p>
<p>캐시: <code>Overwrite/SKSE/Plugins/UniversalHotkeyManager/scan-cache-v1.bin</code> 및 <code>last-scan-v1.bin</code>. 게임을 끄고 둘 다 삭제하면 다음 전체 스캔에서 다시 생성됩니다.</p>

<h3>필수 요구 사항</h3>

<ul>
  <li>Skyrim Special Edition 또는 Anniversary Edition</li>
  <li>SKSE64</li>
  <li>Address Library / CommonLib 호환 런타임</li>
  <li>SKSE Menu Framework 2.1.1 이상</li>
  <li>Microsoft Visual C++ Redistributable</li>
</ul>

<p><b>주의:</b> UHM은 모든 파일 속 숫자를 무조건 충돌로 표시하지 않습니다. 실제로 사용 중인 바인딩과 확인 가능한 근거를 우선하며, 편집은 지원되는 활성 설정 출처에만 적용됩니다.</p>
<p>자동 언어는 Windows 사용자 UI 언어와 로케일을 확인하며 한국어·영어·중국어를 지원합니다. 옵션에서 수동 선택도 가능하고 UI 창 투명도는 본체 배경에만 적용됩니다. UTF-8 모드명과 경로는 그대로 지원합니다.</p>
<p><b>Escape</b>를 누르면 관리 창이 닫힙니다. 편집 입력 중에는 Escape가 해당 입력 취소에 사용됩니다.</p>
<h3>라이선스와 소스</h3>
<p>UHM은 <b>GPL-3.0-or-later</b> 자유 소프트웨어입니다. 1.0.2부터 Nexus에는 MO2용 Release ZIP만 올리며, 바이너리에 대응하는 전체 소스·빌드 스크립트·버전 태그는 <a href="https://github.com/compilecraftworks/Universal-Hotkey-Manager-for-Skyrim-SE-AE">GitHub 공개 저장소</a>에서 제공합니다. 설치한 버전과 같은 태그(예: <b>v1.0.2</b>)를 선택하면 됩니다. Release ZIP에는 UHM 전체 라이선스와 서드파티 고지·라이선스 원문이 들어 있습니다. 마우스·게임패드 라인 그림은 UHM용으로 직접 제작한 원본 GPL-3.0-or-later 자산입니다.</p>
<h3>크레딧</h3>
<ul>
  <li><a href="https://github.com/CharmedBaryon/CommonLibSSE-NG">CommonLibSSE-NG</a> - MIT</li>
  <li><a href="https://github.com/Thiago099/SKSE-Menu-Framework">SKSE Menu Framework</a> - MIT, 런타임 선행 모드</li>
  <li><a href="https://github.com/ocornut/imgui">Dear ImGui</a> / <a href="https://github.com/cimgui/cimgui">cimgui</a> - MIT</li>
  <li><a href="https://github.com/fmtlib/fmt">fmt</a> / <a href="https://github.com/gabime/spdlog">spdlog</a> - MIT</li>
  <li><a href="https://github.com/herumi/xbyak">Xbyak</a> / <a href="https://github.com/d99kris/rapidcsv">rapidcsv</a> - BSD-3-Clause</li>
  <li><a href="https://github.com/lz4/lz4">LZ4</a> - BSD-2-Clause, <a href="https://zlib.net/">zlib</a> - zlib License</li>
  <li><a href="https://github.com/zyantific/zydis">Zydis</a> / <a href="https://github.com/zyantific/zycore-c">Zycore</a> - MIT</li>
  <li><a href="https://skse.silverlock.org/">SKSE64</a> / Address Library for SKSE Plugins - 런타임 선행 모드</li>
</ul>
```
