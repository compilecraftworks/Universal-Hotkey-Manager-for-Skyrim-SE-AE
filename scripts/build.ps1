[CmdletBinding()]
param(
    [switch]$WithSkse,
    [string]$MenuFrameworkRoot = ''
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
$menuFrameworkArgument = if ($WithSkse) { '-DUHI_ENABLE_MENU_FRAMEWORK=ON' } else { '-DUHI_ENABLE_MENU_FRAMEWORK=OFF' }

if ($WithSkse) {
    $dependencyRoot = Join-Path $PSScriptRoot '..\.deps'
    $vcpkgRoot = Join-Path $dependencyRoot 'vcpkg'
    $vcpkgExecutable = Join-Path $vcpkgRoot 'vcpkg.exe'
    if (-not (Test-Path -LiteralPath $vcpkgExecutable)) {
        New-Item -ItemType Directory -Path $dependencyRoot -Force | Out-Null
        if (-not (Test-Path -LiteralPath (Join-Path $vcpkgRoot '.git'))) {
            & git clone https://github.com/microsoft/vcpkg.git $vcpkgRoot
            if ($LASTEXITCODE -ne 0) { throw 'Failed to clone vcpkg.' }
        }
        & git -C $vcpkgRoot checkout 2f1d605400c8727cc00c15797aba796c88ccd523
        if ($LASTEXITCODE -ne 0) { throw 'Failed to select the pinned vcpkg revision.' }
        & (Join-Path $vcpkgRoot 'bootstrap-vcpkg.bat') -disableMetrics
        if ($LASTEXITCODE -ne 0) { throw 'Failed to bootstrap vcpkg.' }
    }
    $toolchain = Join-Path $vcpkgRoot 'scripts\buildsystems\vcpkg.cmake'
    $toolchainArgument = '-DCMAKE_TOOLCHAIN_FILE="{0}" -DVCPKG_TARGET_TRIPLET=x64-windows-static-md' -f $toolchain
}

$command = 'call "{0}" -arch=x64 && "{1}" -S . -B {6} -G Ninja -DCMAKE_MAKE_PROGRAM="{2}" -DCMAKE_CXX_COMPILER=cl -DCMAKE_BUILD_TYPE=RelWithDebInfo -DUHI_BUILD_TESTS=ON -DUHI_BUILD_SKSE={3} {5} {7} && "{1}" --build {6} --clean-first && "{4}" --test-dir {6} --output-on-failure' -f $devCommand, $cmake, $ninja, $skseFlag, $ctest, $toolchainArgument, $buildDirectory, $menuFrameworkArgument
& cmd.exe /d /s /c $command
if ($LASTEXITCODE -ne 0) {
    throw "UHI build failed with exit code $LASTEXITCODE."
}
