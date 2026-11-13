[CmdletBinding()]
param([ValidateSet('Debug','Release')][string]$Configuration='Release', [switch]$Sanitize,
      [string]$Distribution='')
$ErrorActionPreference = 'Stop'
$Root = Split-Path $PSScriptRoot -Parent
if (-not (Get-Command wsl.exe -ErrorAction SilentlyContinue)) { throw 'WSL2 is required for this Linux/webOS native project.' }
$WslArgs = if ($Distribution) { @('-d', $Distribution) } else { @() }
$LinuxRoot = (& wsl.exe @WslArgs -- wslpath -a -u $Root).Trim()
if ($LASTEXITCODE -ne 0 -or -not $LinuxRoot) { throw 'Could not resolve project path in WSL.' }
$Build = "$LinuxRoot/build/desktop" + $(if ($Sanitize) { '-asan' } else { '' })
$San = if ($Sanitize) { 'ON' } else { 'OFF' }
& wsl.exe @WslArgs -- cmake -S $LinuxRoot -B $Build -G Ninja "-DCMAKE_BUILD_TYPE=$Configuration" '-DSYNERGY_STRICT=ON' "-DSYNERGY_SANITIZE=$San"
if ($LASTEXITCODE -ne 0) { throw 'CMake configure failed.' }
& wsl.exe @WslArgs -- cmake --build $Build --parallel 4
if ($LASTEXITCODE -ne 0) { throw 'Native build failed.' }
$FixtureRoot = (& wsl.exe @WslArgs -- mktemp -d /tmp/sy041.XXXXXX).Trim()
if ($LASTEXITCODE -ne 0 -or $FixtureRoot -notmatch '^/tmp/sy041\.[A-Za-z0-9]{6}$') { throw 'Could not create private POSIX test fixtures.' }
try {
    & wsl.exe @WslArgs -- env "SYNERGY_TEST_POSIX_ROOT=$FixtureRoot" "TMPDIR=$FixtureRoot" ctest --test-dir $Build --output-on-failure
    if ($LASTEXITCODE -ne 0) { throw 'Native tests failed.' }
} finally {
    & wsl.exe @WslArgs -- rm -rf -- $FixtureRoot
}
Write-Host "Host build and tests complete: $Build. This is not a TV binary."
