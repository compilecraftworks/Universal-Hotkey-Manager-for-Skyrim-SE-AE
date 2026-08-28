# Native Dear ImGui integration boundary

UHM keeps scanning, classification, serialization, and view-model logic
independent of Direct3D. `NativeImGuiHost` owns one Skyrim `IMenu`, one Dear
ImGui context, and the official Win32/DX11 backend lifecycle.

- Skyrim's UI task queue owns menu show/hide transitions.
- The menu uses Skyrim's menu input context, cursor, modal, and pause flags.
- GFx keyboard, mouse, wheel, and gamepad events are translated into Dear ImGui
  input without stopping Skyrim's `MenuControls` from receiving ordinary menu
  navigation.
- Opening-shortcut, Escape, popup, and capture events are consumed exactly once
  so they cannot trigger gameplay or Skyrim's system menu underneath UHM.
- Popup dimming remains transparent and the existing UHM layout is rendered by
  `MenuFrameworkAdapter` as a compatibility name for the UI presenter only; it
  no longer talks to an external framework.
