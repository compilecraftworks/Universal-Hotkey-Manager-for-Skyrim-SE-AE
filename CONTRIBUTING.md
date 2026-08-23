# Contributing

Thank you for improving Universal Hotkey Manager for Skyrim SE-AE.

## Development setup

1. Install Visual Studio 2022 with the Desktop development with C++ workload.
2. Install CMake 3.24 or newer and Git.
3. Obtain the SKSE Menu Framework SDK and pass its directory to `scripts/build.ps1` when it is not already available locally.
4. Build and run the regression suite:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\build.ps1 -WithSkse `
  -MenuFrameworkRoot "C:\path\to\SKSE-Menu-Framework-SDK"
```

Generated dependencies, build trees, caches, release archives, and local IDE files must not be committed. The pinned vcpkg manifest and baseline are the authoritative dependency inputs.

## Changes

- Keep scanning bounded and CTD-conscious. Do not add runtime detours or signature hooks without an explicit design review.
- Preserve source-format-aware hotkey serialization and transactional rollback.
- Add or update automated tests for scanner, decoder, writer, cache, context, or UI-state changes.
- Update `CHANGELOG.md`, `CHANGELOG_KO.md`, and relevant user documentation for visible behavior changes.
- Do not commit third-party binaries or SDKs. Preserve their copyright and license notices.

## Licensing of contributions

By submitting a contribution, you agree to license it under GNU GPL version 3 or later, the same license as this project. Do not submit code or assets you do not have the right to redistribute. Third-party material must be clearly identified and license-compatible.
