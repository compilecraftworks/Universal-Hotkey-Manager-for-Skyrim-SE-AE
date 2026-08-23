# SKSE Menu Framework integration boundary

UHI keeps its scan and view-model code independent of D3D and ImGui. The
future SKSE Menu Framework adapter should register one render callback and
forward it to `UHI::HotkeyViewRenderer::Render()`:

1. Build a `Registry` from the last completed scan.
2. Set `HotkeyViewState::open` from the UHI toggle key.
3. Draw the category/device tabs in ImGui.
4. Pass text and entry lambdas to `Render()`.
5. Use `HotkeyViewEntry::record` for the full tooltip and evidence path.

The current build intentionally does not require the external framework SDK.
Once the SDK is installed, only the adapter target and plugin registration need
to be enabled.
