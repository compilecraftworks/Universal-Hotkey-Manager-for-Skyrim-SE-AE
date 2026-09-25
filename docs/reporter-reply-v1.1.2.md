Thank you for reporting this. Skyrim AE 1.6.1170 is supported.

Are you using Japanese or Chinese in Windows or Skyrim? In particular, which Windows display language do you use, and have you changed the default Windows UI font?

I found a bug introduced in 1.0.7 when UHM began using the Windows UI font. Some Japanese and Chinese fonts contain several font faces in a single file. UHM was reading only part of that file, causing it to look up font data at the wrong locations. This could crash the game during startup, before the UHM menu was opened. I reproduced the crash with MS UI Gothic, Yu Gothic UI, and Microsoft JhengHei UI.

The fix is included in 1.1.2 and has passed local regression tests. Please try updating. This may explain your issue, but I cannot confirm it is the same crash without your logs. If it still crashes, please share UniversalHotkeyManager.log, skse64.log, and any Crash Logger report from the failed launch.
