[CmdletBinding()]
param(
    [switch]$WithSkse
)

$ErrorActionPreference = 'Stop'

$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
if (-not (Test-Path -LiteralPath $vswhere)) {
    throw 'Visual Studio Installer (vswhere.exe) was not found.'
}

$vsRoot = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $vsRoot) {
    throw 'Visual Studio 2022 C++ Build Tools were not found.'
}

$devCommand = Join-Path $vsRoot 'Common7\Tools\VsDevCmd.bat'
$cmake = Join-Path $vsRoot 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
$ctest = Join-Path $vsRoot 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\ctest.exe'
$ninja = Join-Path $vsRoot 'Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe'
$skseFlag = if ($WithSkse) { 'ON' } else { 'OFF' }
$buildDirectory = if ($WithSkse) { 'build/skse' } else { 'build/core' }
$toolchainArgument = ''

if ($WithSkse) {
    $dependencyRoot = Join-Path $PSScriptRoot '..\.deps'
    $vcpkgRoot = Join-Path $dependencyRoot 'vcpkg'
    $vcpkgExecutable = Join-Path $vcpkgRoot 'vcpkg.exe'
    $vcpkgBootstrapStamp = Join-Path $vcpkgRoot '.uhi-bootstrap-revision'
    # Keep the build tool itself reproducible as well as the manifest graph.
    # An already-present vcpkg directory must not silently keep a newer or
    # older checkout than the pinned revision below.
    $vcpkgRevision = '2f1d605400c8727cc00c15797aba796c88ccd523'
    $bootstrapVcpkg = $false
    if (-not (Test-Path -LiteralPath (Join-Path $vcpkgRoot '.git'))) {
        New-Item -ItemType Directory -Path $dependencyRoot -Force | Out-Null
        & git clone https://github.com/microsoft/vcpkg.git $vcpkgRoot
        if ($LASTEXITCODE -ne 0) { throw 'Failed to clone vcpkg.' }
        $bootstrapVcpkg = $true
    }

    $vcpkgHead = & git -c "safe.directory=$vcpkgRoot" -C $vcpkgRoot rev-parse HEAD
    if ($LASTEXITCODE -ne 0) { throw 'Failed to read the vcpkg revision.' }
    if ($vcpkgHead.Trim() -ne $vcpkgRevision) {
        & git -c "safe.directory=$vcpkgRoot" -C $vcpkgRoot checkout --detach $vcpkgRevision
        if ($LASTEXITCODE -ne 0) { throw 'Failed to select the pinned vcpkg revision.' }
        $bootstrapVcpkg = $true
    }

    if (-not $bootstrapVcpkg) {
        if (-not (Test-Path -LiteralPath $vcpkgExecutable) -or
            -not (Test-Path -LiteralPath $vcpkgBootstrapStamp) -or
            (Get-Content -LiteralPath $vcpkgBootstrapStamp -Raw).Trim() -ne $vcpkgRevision) {
            # vcpkg.exe is a separately versioned, signature-validated bootstrap
            # artifact. Record which pinned source checkout produced it instead
            # of incorrectly comparing its release version to the git revision.
            $bootstrapVcpkg = $true
        }
    }

    if ($bootstrapVcpkg -or -not (Test-Path -LiteralPath $vcpkgExecutable)) {
        & (Join-Path $vcpkgRoot 'bootstrap-vcpkg.bat') -disableMetrics
        if ($LASTEXITCODE -ne 0) { throw 'Failed to bootstrap vcpkg.' }
        Set-Content -LiteralPath $vcpkgBootstrapStamp -Value $vcpkgRevision -NoNewline
    }
    $toolchain = Join-Path $vcpkgRoot 'scripts\buildsystems\vcpkg.cmake'
    $overlayPorts = Join-Path $PSScriptRoot '..\vcpkg-ports'
    $toolchainArgument = '-DCMAKE_TOOLCHAIN_FILE="{0}" -DVCPKG_TARGET_TRIPLET=x64-windows-static-md -DVCPKG_OVERLAY_PORTS="{1}"' -f $toolchain, $overlayPorts
}

$command = 'call "{0}" -arch=x64 && "{1}" -S . -B {6} -G Ninja -DCMAKE_MAKE_PROGRAM="{2}" -DCMAKE_CXX_COMPILER=cl -DCMAKE_BUILD_TYPE=RelWithDebInfo -DUHI_BUILD_TESTS=ON -DUHI_BUILD_SKSE={3} {5} && "{1}" --build {6} --clean-first && "{4}" --test-dir {6} --output-on-failure' -f $devCommand, $cmake, $ninja, $skseFlag, $ctest, $toolchainArgument, $buildDirectory
& cmd.exe /d /s /c $command
if ($LASTEXITCODE -ne 0) {
    throw "UHI build failed with exit code $LASTEXITCODE."
}
