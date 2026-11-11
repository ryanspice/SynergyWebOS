[CmdletBinding()]
param([ValidateSet('Debug','Release')][string]$Configuration='Release', [switch]$Sanitize)
$ErrorActionPreference = 'Stop'
$Root = Split-Path $PSScriptRoot -Parent
if (-not (Get-Command wsl.exe -ErrorAction SilentlyContinue)) { throw 'WSL2 is required for this Linux/webOS native project.' }
$LinuxRoot = (& wsl.exe wslpath -a -u $Root).Trim()
if ($LASTEXITCODE -ne 0 -or -not $LinuxRoot) { throw 'Could not resolve project path in WSL.' }
$Build = "$LinuxRoot/build/desktop" + $(if ($Sanitize) { '-asan' } else { '' })
$San = if ($Sanitize) { 'ON' } else { 'OFF' }
& wsl.exe -- cmake -S $LinuxRoot -B $Build -G Ninja "-DCMAKE_BUILD_TYPE=$Configuration" '-DSYNERGY_STRICT=ON' "-DSYNERGY_SANITIZE=$San"
if ($LASTEXITCODE -ne 0) { throw 'CMake configure failed.' }
& wsl.exe -- cmake --build $Build --parallel 4
if ($LASTEXITCODE -ne 0) { throw 'Native build failed.' }
& wsl.exe -- ctest --test-dir $Build --output-on-failure
if ($LASTEXITCODE -ne 0) { throw 'Native tests failed.' }
Write-Host "Host build and tests complete: $Build. This is not a TV binary."
