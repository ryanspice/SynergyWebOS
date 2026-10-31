<#
.SYNOPSIS
  Acceptance-test runner for synergy-tv (QA for the app, dock, home patch, hot-corner tooling).

.DESCRIPTION
  Discovers tests/**/*.Tests.ps1. Each file returns an ordered map of test name -> a
  scriptblock; the block asserts and throws on failure (or uses the helpers below).
  No Pester installation is required: this is a ~a small deterministic harness so the
  checks run on a bare Windows checkout and in CI. If Pester 5 IS installed with
  -UsePester, the same test files are executed through Pester instead.

  Suites:
    unit        Pure functions/fixtures (fast, no app needed)
    acceptance  Product contracts for app/, dock/, scripts/home/, tools/synergy/
    integration Fixture-based end-to-end (staged build, IPK when app/ is ready)

  Exit codes: 0 = all executed checks pass; 1 = failures; 2 = a required check could
  not run (BLOCKED) and failures do not hide it.

.EXAMPLE
  pwsh -NoProfile -File tests/lib/Run-Tests.ps1
.EXAMPLE
  powershell -NoProfile -File tests/lib/Run-Tests.ps1 -Tag unit -Verbose
#>
[CmdletBinding()]
param(
    [ValidateSet('all', 'unit', 'acceptance', 'integration')][string[]]$Tag = @('all'),
    [string]$Filter,
    [switch]$UsePester,
    [switch]$Json,
    [string]$ReportPath
)

. (Join-Path $PSScriptRoot '..\..\scripts\build\lib\Common.ps1')
. (Join-Path $PSScriptRoot '..\..\scripts\build\lib\Checks.ps1')
. (Join-Path $PSScriptRoot 'Assert.ps1')
. (Join-Path $PSScriptRoot 'Ipk.ps1')

$testsRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).ProviderPath
$results = New-Object System.Collections.Generic.List[object]

function Add-Result {
    param([string]$Suite, [string]$Name, [string]$Status, [string]$Detail)
    $results.Add([pscustomobject]@{ suite = $Suite; name = $Name; status = $Status; detail = $Detail })
}

function Test-TagSelected {
    param([string]$Suite)
    if ($Tag -contains 'all' -or $Tag.Count -eq 0) { return $true }
    return $Tag -contains $Suite
}

# Optional Pester bridge: same test source, Pester lifecycle.
if ($UsePester) {
    $pester = Get-Module -ListAvailable Pester | Sort-Object Version -Descending | Select-Object -First 1
    if (-not $pester -or $pester.Version.Major -lt 5) {
        Write-Fail '-UsePester requested but Pester 5 is not installed. Not installing dependencies; rerun without -UsePester.'
        exit 2
    }
    Import-Module Pester -MinimumVersion 5.0 -ErrorAction Stop
    [Console]::WriteLine("Pester $($pester.Version) bridge: executing shared test bodies via Pester Describe/It.")
    foreach ($file in (Get-ChildItem -Path $testsRoot -Recurse -Filter '*.Tests.ps1' -File)) {
        $suite = $file.Directory.Name
        if (-not (Test-TagSelected $suite)) { continue }
        $map = & $file.FullName
        if (-not $map) { continue }
        $describeName = $file.BaseName
        Invoke-Pester -Script @([pscustomobject]@{
                Path   = ''
                ScriptBlock = {
                    foreach ($kv in $map.GetEnumerator()) {
                        if ($Filter -and $kv.Key -notmatch $Filter) { continue }
                        It $kv.Key { & $kv.Value }
                    }
                }
            }) -Output Detailed -PassThru | Out-Null
        break
    }
    exit 0
}

# Determine availability of another delegate's scope up front so BLOCKED is explicit.
$appDir = Join-Path (Get-RepoRoot) 'app'
$appReady = Test-Path -LiteralPath (Join-Path $appDir 'appinfo.json')
$dock = Get-DockArtifacts
$homeArt = Get-HomeArtifacts
$cornerFiles = Get-CornerToolingFiles

$dockState = if ($dock.Exists) { 'present' } else { 'missing' }
$homeState = if ($homeArt.ClientPy) { Get-RelativeRepoPath $homeArt.ClientPy } else { 'missing' }
$cornerCount = @($cornerFiles).Count
[Console]::WriteLine("synergy-tv tests  repo=$(Get-RepoRoot)")
[Console]::WriteLine("app ready=$appReady  dock files=$dockState  home patch=$homeState  tools/synergy files=$cornerCount")

$stopwatch = [System.Diagnostics.Stopwatch]::StartNew()
foreach ($file in (Get-ChildItem -Path $testsRoot -Recurse -Filter '*.Tests.ps1' -File | Sort-Object FullName)) {
    $suite = $file.Directory.Name
    if (-not (Test-TagSelected $suite)) { continue }
    $fileRel = Get-RelativeRepoPath $file.FullName
    # Load the test file in the CURRENT scope so its helper functions/variables are
    # visible to the test blocks. The map itself is the only output, so output
    # produced while loading is discarded.
    $map = $null
    try {
        . $file.FullName | Out-Null
    } catch {
        Add-Result -Suite $suite -Name $fileRel -Status 'FAIL' -Detail ("test file failed to load: " + $_.Exception.Message)
        continue
    }
    if (-not ($map -is [System.Collections.IDictionary])) {
        Add-Result -Suite $suite -Name $fileRel -Status 'FAIL' -Detail 'test file must assign an ordered map to $map'
        continue
    }
    foreach ($kv in $map.GetEnumerator()) {
        $testName = $kv.Key
        if ($Filter -and $testName -notmatch $Filter) { continue }
        $block = $kv.Value
        $global:LASTEXITCODE = 0
        $t = [System.Diagnostics.Stopwatch]::StartNew()
        # Strings are concatenated, never -f formatted: assertion messages can contain {0}.
        try {
            $output = & $block 2>&1
            $detail = (@($output | ForEach-Object { "$_" }) -join ' ')
            if ($LASTEXITCODE -ne 0) { throw "child command exit $LASTEXITCODE" }
            Add-Result -Suite $suite -Name $testName -Status 'PASS' -Detail $detail
            [Console]::WriteLine("PASS  [" + $suite + "] " + $testName + "  (" + $t.ElapsedMilliseconds + " ms)")
        } catch {
            $detail = $_.Exception.Message
            if ($detail -match '^SKIP:') {
                Add-Result -Suite $suite -Name $testName -Status 'SKIP' -Detail $detail
                [Console]::WriteLine("SKIP  [" + $suite + "] " + $testName + ": " + ($detail -replace '^SKIP:\s*', ''))
            } elseif ($detail -match '^BLOCKED:') {
                Add-Result -Suite $suite -Name $testName -Status 'BLOCKED' -Detail $detail
                [Console]::WriteLine("BLOCK [" + $suite + "] " + $testName + ": " + ($detail -replace '^BLOCKED:\s*', ''))
            } else {
                Add-Result -Suite $suite -Name $testName -Status 'FAIL' -Detail $detail
                [Console]::WriteLine("FAIL  [" + $suite + "] " + $testName)
                [Console]::WriteLine("      " + $detail)
            }
        }
    }
}
$stopwatch.Stop()

$pass = @($results | Where-Object Status -eq 'PASS').Count
$fail = @($results | Where-Object Status -eq 'FAIL').Count
$skip = @($results | Where-Object Status -eq 'SKIP').Count
if ($results.Count -eq 0) {
    Write-Fail "no tests discovered or selected under $testsRoot (Tag=$($Tag -join ','), Filter='$Filter')."
    exit 2
}
$blocked = @($results | Where-Object Status -eq 'BLOCKED').Count

[Console]::WriteLine("")
$statusLine = "TESTS  pass=$pass fail=$fail skip=$skip blocked=$blocked  total=$($results.Count)  ({0:N1}s)" -f $stopwatch.Elapsed.TotalSeconds
if ($fail -gt 0) { [Console]::Error.WriteLine($statusLine) } else { [Console]::WriteLine($statusLine) }
if ($blocked) {
    $results | Where-Object Status -eq 'BLOCKED' | ForEach-Object { [Console]::WriteLine("  BLOCKED " + $_.name + ": " + ($_.detail -replace '^BLOCKED:\s*', '')) }
}
if ($skip) {
    $results | Where-Object Status -eq 'SKIP' | ForEach-Object { [Console]::WriteLine("  SKIP    " + $_.name + ": " + ($_.detail -replace '^SKIP:\s*', '')) }
}

$summary = @{}
$summary['dateUtc'] = (Get-Date).ToUniversalTime().ToString('yyyy-MM-ddTHH:mm:ssZ')
$summary['repo'] = Get-RepoRoot
$summary['pass'] = $pass
$summary['fail'] = $fail
$summary['skip'] = $skip
$summary['blocked'] = $blocked
$summary['tags'] = @($Tag)
$summary['appReady'] = [bool]$appReady
$summary['results'] = @($results | ForEach-Object { [pscustomobject]@{ suite = "$($_.suite)"; name = "$($_.name)"; status = "$($_.status)"; detail = "$($_.detail)" } })
if ($ReportPath) {
    [System.IO.File]::WriteAllText((Resolve-RelPath $ReportPath), ($summary | ConvertTo-Json -Depth 6), [System.Text.UTF8Encoding]::new($false))
}
if ($Json) { $summary | ConvertTo-Json -Depth 6 }

if ($fail -gt 0) { exit 1 }
if ($blocked -gt 0) {
    Write-Warn "$blocked required check(s) BLOCKED on a missing dependency; see above."
    exit 2
}
exit 0
