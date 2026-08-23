# Device view and hotkey manager

UHM has two complementary views inside the same manager window:

1. Device view — keyboard, mouse, and gamepad diagrams with compact action labels.
2. Hotkey manager — sortable binding, owner, source, context, editability, confidence, and conflict columns.

The UHM manager opens with `Del` by default. The toolbar switches between Device
view and Hotkey manager without rescanning; both render the current validated
result snapshot.

## Layout

The default layout places the complete keyboard above two lower device cards:

    KEYBOARD (full physical layout)
    W  Forward      F6  Menu toggle      Del  Open manager

    MOUSE                         GAMEPAD
    Left Click  Attack            A         Activate
    Side BT DN Lock-on            D-pad UP  Previous item

Only mapped bindings with a non-empty action are shown. Category, device,
search, confirmed-conflict, potential-overlap, and source-detail controls filter
the same result. SexLab entries appear only when the dependency is detected.

Each row has a small status marker:

- confirmed
- candidate/inferred
- editable
- conflict

Supported effective loose configuration bindings can be edited from either the
list or device context menu. The writer verifies the original value, makes a
first-write `.uhi.bak`, replaces atomically, and rescans. Hard-coded PEX/DLL and
unsupported sources stay read-only.

## Compact labels and tooltips

Keycaps use compact labels so the physical keyboard geometry is never distorted:

- Bksp -> Backspace
- PgUp / PgDn -> Page Up / Page Down
- PrtSc -> Print Screen
- ScrLk -> Scroll Lock
- NumLk -> Num Lock
- Ins / Del -> Insert / Delete
- LShift / RShift -> Left / Right Shift
- LCtrl / RCtrl -> Left / Right Ctrl
- Win -> Windows key
- Num 0-9 -> numeric keypad digits

- CS -> Community Shaders
- SL -> SexLab
- UHM -> Universal Hotkey Manager for Skyrim SE-AE
- Atk L / Atk R -> Attack Left / Attack Right
- P/Sh -> Power / Shout
- R/S -> Ready / Sheath

When the cursor rests on a key, a tooltip shows the complete record:

    F6
    Community Shaders - Toggle shader menu
    Category: External tools
    Source: SettingsUser.json
    Status: Confirmed - Editable

Tooltips may include the full action, owning mod, category, source file,
confidence, conflict list and whether the binding is editable. This keeps the
keyboard readable while retaining complete information on demand.
