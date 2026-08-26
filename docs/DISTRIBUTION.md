# Distribution and corresponding source

Universal Hotkey Manager for Skyrim SE-AE is distributed under GPL-3.0-or-later.

## Nexus package policy from 1.0.2

Nexus receives one MO2-ready binary archive:

`Universal Hotkey Manager for Skyrim SE-AE <version>.zip`

The archive contains the plugin, default configuration, original device artwork, project `LICENSE`, user documentation, `THIRD_PARTY_NOTICES.md`, and the exact license texts required by the bundled or statically linked third-party components. It does not contain a second source ZIP.

## Corresponding source

The complete corresponding source is the public repository at:

<https://github.com/compilecraftworks/Universal-Hotkey-Manager-for-Skyrim-SE-AE>

Each published binary must have a matching immutable Git tag such as `v1.0.5`. The tag contains the source, CMake files, pinned vcpkg metadata, tests, resources, packaging scripts, and notices used for that release. GitHub's automatically generated tag archives may be used as source downloads.

Before publishing a binary:

1. Build and run all tests.
2. Run `scripts/package.ps1` and verify `SHA256SUMS.txt`.
3. Confirm that the DLL ProductVersion, package version, changelog, and Git tag match.
4. Confirm that all required third-party license texts exist in `ThirdPartyLicenses/` inside the Release ZIP.
5. Push the release commit and tag before or at the same time as the Nexus binary.
