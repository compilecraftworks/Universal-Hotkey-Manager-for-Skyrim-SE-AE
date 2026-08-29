[CmdletBinding()]
param(
    [string]$Version = '1.0.6',
    [string]$BuildDirectory = 'build/skse',
    [string]$OutputDirectory = 'releases'
)

$ErrorActionPreference = 'Stop'

$projectRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$buildRoot = [System.IO.Path]::GetFullPath((Join-Path $projectRoot $BuildDirectory))
$outputRoot = [System.IO.Path]::GetFullPath((Join-Path $projectRoot $OutputDirectory))
$stagingRoot = Join-Path $outputRoot '.staging'
$releaseStage = Join-Path $stagingRoot 'release'
$sourceStage = Join-Path $stagingRoot 'source'
$dllSource = Join-Path $buildRoot 'UniversalHotkeyManager.dll'

foreach ($path in @($buildRoot, $outputRoot, $stagingRoot, $releaseStage, $sourceStage)) {
    if (-not $path.StartsWith($projectRoot, [System.StringComparison]::OrdinalIgnoreCase)) {
        throw "Packaging path escapes the project directory: $path"
    }
}

if (-not (Test-Path -LiteralPath $dllSource)) {
    throw "Built plugin was not found: $dllSource"
}

$productVersion = (Get-Item -LiteralPath $dllSource).VersionInfo.ProductVersion
if ($productVersion -ne $Version) {
    throw "Built DLL version is '$productVersion'; expected '$Version'. Rebuild before packaging."
}

# Dear ImGui is statically linked into UHM. Importing a standalone imgui.dll
# would create an unsupported second UI runtime, so refuse such a binary.
$dllAscii = [System.Text.Encoding]::ASCII.GetString([System.IO.File]::ReadAllBytes($dllSource))
if ($dllAscii.IndexOf('imgui.dll', [System.StringComparison]::OrdinalIgnoreCase) -ge 0) {
    throw 'Built DLL still imports the legacy imgui.dll; refusing to package an unsafe UI binary.'
}

New-Item -ItemType Directory -Path $outputRoot -Force | Out-Null
if (Test-Path -LiteralPath $stagingRoot) {
    Remove-Item -LiteralPath $stagingRoot -Recurse -Force
}
New-Item -ItemType Directory -Path (Join-Path $releaseStage 'SKSE/Plugins') -Force | Out-Null
New-Item -ItemType Directory -Path (Join-Path $releaseStage 'SKSE/Plugins/UniversalHotkeyManager/assets') -Force | Out-Null
New-Item -ItemType Directory -Path (Join-Path $releaseStage 'ThirdPartyLicenses') -Force | Out-Null
New-Item -ItemType Directory -Path $sourceStage -Force | Out-Null

Copy-Item -LiteralPath $dllSource -Destination (Join-Path $releaseStage 'SKSE/Plugins/UniversalHotkeyManager.dll')
Copy-Item -LiteralPath (Join-Path $projectRoot 'config/UniversalHotkeyManager.ini') `
    -Destination (Join-Path $releaseStage 'SKSE/Plugins/UniversalHotkeyManager.ini')
Copy-Item -LiteralPath (Join-Path $projectRoot 'assets/mouse.png') `
    -Destination (Join-Path $releaseStage 'SKSE/Plugins/UniversalHotkeyManager/assets/mouse.png')
Copy-Item -LiteralPath (Join-Path $projectRoot 'assets/gamepad.png') `
    -Destination (Join-Path $releaseStage 'SKSE/Plugins/UniversalHotkeyManager/assets/gamepad.png')

$rootDocuments = @(
    # Keep the release ZIP deliberately lean. Installation instructions,
    # screenshots, changelogs, and publishing copy live with the tagged
    # source on GitHub. Retain only the licensing/source notice required for
    # a GPL binary distribution.
    'LICENSE', 'THIRD_PARTY_NOTICES.md'
)
foreach ($name in $rootDocuments) {
    Copy-Item -LiteralPath (Join-Path $projectRoot $name) -Destination $releaseStage
}

$licensePackages = @(
    'commonlibsse-ng', 'imgui', 'fmt', 'spdlog', 'xbyak', 'zlib', 'lz4', 'zydis', 'zycore', 'rapidcsv'
)
$installedShare = Join-Path $buildRoot 'vcpkg_installed/x64-windows-static-md/share'
foreach ($package in $licensePackages) {
    $licenseSource = Join-Path $installedShare "$package/copyright"
    if (-not (Test-Path -LiteralPath $licenseSource)) {
        throw "Required third-party license was not found: $licenseSource"
    }
    $licenseName = "$package.txt"
    Copy-Item -LiteralPath $licenseSource -Destination (Join-Path $releaseStage "ThirdPartyLicenses/$licenseName")
}

$releaseZip = Join-Path $outputRoot "Universal Hotkey Manager for Skyrim SE-AE $Version.zip"
$sourceZip = Join-Path $outputRoot "Universal Hotkey Manager for Skyrim SE-AE $Version - Source.zip"
if (Test-Path -LiteralPath $releaseZip) {
    Remove-Item -LiteralPath $releaseZip -Force
}
if (Test-Path -LiteralPath $sourceZip) {
    Remove-Item -LiteralPath $sourceZip -Force
}

Compress-Archive -Path (Join-Path $releaseStage '*') -DestinationPath $releaseZip -CompressionLevel Optimal

$sourceFiles = @(
    '.gitattributes', '.gitignore',
    'CHANGELOG.md', 'CHANGELOG_KO.md', 'CMakeLists.txt', 'CMakePresets.json',
    'CONTRIBUTING.md', 'INSTALL.md', 'LICENSE', 'README.md', 'SECURITY.md',
    'THIRD_PARTY_NOTICES.md', 'vcpkg-configuration.json', 'vcpkg.json'
)
foreach ($name in $sourceFiles) {
    $source = Join-Path $projectRoot $name
    if (-not (Test-Path -LiteralPath $source)) {
        throw "Required source file was not found: $source"
    }
    Copy-Item -LiteralPath $source -Destination $sourceStage
}

$sourceDirectories = @(
    '.github', 'assets', 'config', 'docs', 'extras', 'include', 'mcm',
    'resources', 'scripts', 'src', 'tests', 'vcpkg-ports'
)
foreach ($name in $sourceDirectories) {
    $source = Join-Path $projectRoot $name
    if (-not (Test-Path -LiteralPath $source)) {
        throw "Required source directory was not found: $source"
    }
    Copy-Item -LiteralPath $source -Destination $sourceStage -Recurse -Force
}

Compress-Archive -Path (Join-Path $sourceStage '*') -DestinationPath $sourceZip -CompressionLevel Optimal

$hashes = Get-FileHash -LiteralPath $releaseZip, $sourceZip -Algorithm SHA256
$checksums = $hashes | ForEach-Object {
    '{0}  {1}' -f $_.Hash, (Split-Path -Leaf $_.Path)
}
$checksumPath = Join-Path $outputRoot 'SHA256SUMS.txt'
[System.IO.File]::WriteAllLines($checksumPath, $checksums, [System.Text.UTF8Encoding]::new($false))

Remove-Item -LiteralPath $stagingRoot -Recurse -Force

Get-Item -LiteralPath $releaseZip, $sourceZip, $checksumPath |
    Select-Object FullName, Length, LastWriteTime
