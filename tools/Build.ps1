#requires -version 7.0
[CmdletBinding()]
param(
    [switch]$Deploy,
    [switch]$Fresh,
    [string]$VsRoot = 'C:\Program Files\Microsoft Visual Studio\18\Insiders',
    [string]$QtRoot = 'C:\Qt\6.12.0\msvc2022_64'
)
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path $PSScriptRoot -Parent
$vcvars = Join-Path $VsRoot 'VC\Auxiliary\Build\vcvars64.bat'
$cmakeFolder = Join-Path $VsRoot 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin'
$ninjaFolder = Join-Path $VsRoot 'Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja'
if (-not (Test-Path -LiteralPath $vcvars)) { throw "VS2026 vcvars64.bat not found: $vcvars" }
$environmentCommand = 'call "' + $vcvars + '" >nul && set'
$environmentLines = & $env:ComSpec /d /c $environmentCommand
if ($LASTEXITCODE -ne 0) { throw 'VS2026 environment initialization failed.' }
foreach ($line in $environmentLines) {
    if ($line -match '^([^=]+)=(.*)$') { [Environment]::SetEnvironmentVariable($Matches[1], $Matches[2], 'Process') }
}
$env:VSLANG = '1033'
$env:PATH = "$cmakeFolder;$ninjaFolder;$(Join-Path $QtRoot 'bin');$env:PATH"
[void][IO.Directory]::CreateDirectory((Join-Path $projectRoot 'out'))
$probePath = Join-Path $projectRoot 'out\msvc-prefix-probe.cpp'
[IO.File]::WriteAllText($probePath,"#include <stddef.h>`n",[Text.UTF8Encoding]::new($false))
$probeLines = & cl.exe /nologo /utf-8 /EP /showIncludes $probePath 2>&1
if ($LASTEXITCODE -ne 0) { throw 'MSVC dependency-prefix probe failed.' }
$dependencyPrefix = $null
foreach ($probeLine in $probeLines) {
    if ([string]$probeLine -match '^(.*?)[A-Za-z]:\\.*\.(h|hpp)\s*$') {
        $dependencyPrefix = $Matches[1].TrimEnd()
        break
    }
}
if (-not $dependencyPrefix) { throw 'Could not determine the MSVC dependency-prefix text.' }
Push-Location $projectRoot
try {
    $configureArguments = @('--preset', 'vs2026-release', "-DCMAKE_PREFIX_PATH=$QtRoot", "-DCLASSIC_MSVC_DEPENDENCY_PREFIX=$dependencyPrefix")
    if ($Fresh) { $configureArguments = @('--fresh') + $configureArguments }
    & (Join-Path $cmakeFolder 'cmake.exe') @configureArguments
    if ($LASTEXITCODE -ne 0) { throw 'CMake configure failed.' }
    & (Join-Path $cmakeFolder 'cmake.exe') --build --preset vs2026-release
    if ($LASTEXITCODE -ne 0) { throw 'Build failed.' }
    & (Join-Path $cmakeFolder 'ctest.exe') --preset vs2026-release
    $testExitCode = $LASTEXITCODE
    $testReport = Join-Path $projectRoot 'out\build\release\imecore-tests.txt'
    if (Test-Path -LiteralPath $testReport) {
        if ($testExitCode -ne 0) { Get-Content -LiteralPath $testReport -Encoding utf8 }
        else { Get-Content -LiteralPath $testReport -Encoding utf8 | Select-String -Pattern '^Totals:' }
    }
    if ($testExitCode -ne 0) { throw 'Tests failed.' }
    if ($Deploy) {
        & (Join-Path $cmakeFolder 'cmake.exe') --install out/build/release --prefix out/app
        if ($LASTEXITCODE -ne 0) { throw 'Qt deployment failed.' }
        $redistVersion = Get-ChildItem -LiteralPath (Join-Path $VsRoot 'VC\Redist\MSVC') -Directory |
            Where-Object Name -Match '^\d+\.\d+\.\d+$' | Sort-Object { [version]$_.Name } -Descending | Select-Object -First 1
        $runtimeDirectory = Join-Path $redistVersion.FullName 'x64\Microsoft.VC145.CRT'
        if (-not (Test-Path -LiteralPath $runtimeDirectory)) { throw 'VS2026 x64 CRT runtime directory was not found.' }
        Get-ChildItem -LiteralPath $runtimeDirectory -Filter '*.dll' -File | Copy-Item -Destination out/app/bin
    }
} finally { Pop-Location }
