<#
.SYNOPSIS
  Reproducible build + static QA + webOS IPK packaging for the synergy-tv TV app.

.DESCRIPTION
  Stages app/ into a clean directory, runs deterministic static checks (appinfo, HTML,
  CSS, JS syntax, TV static compatibility), then packages with the local ares-cli into
  dist/ and writes dist/<artifact>.ipk.sha256 plus dist/build-info.json (tool versions,
  override list, check results). Never installs on the TV and never edits app/.

  Inputs (all overridable):
    app/               Flash-high app source (read-only to this pipeline)
    app/appinfo.json   Flash-high package metadata (read-only)
    scripts/build/package-manifest.json   build-owned overrides/excludes/targets

  Pipeline overrides of app/appinfo.json are refused (exit 4). Build-owned files may
  be injected by listing them under buildOverrides in the manifest.

.PARAMETER CheckOnly
  Run all checks but do not package.

.PARAMETER SkipPackage
  Alias for -CheckOnly.

.PARAMETER StagingDir
  Directory used for staging (default: dist/staging). Always recreated.

.PARAMETER AresCliDir
  ares-cli install dir (default: B:\Dev\webos\.tools\ares-cli beside the repo, or $env:ARES_CLI_DIR).

.PARAMETER AllowMissingApp
  Scaffold mode: when app/ is absent, run pipeline self-checks only and exit 0 with a
  clear "app not ready" notice (for early integration; NOT a release gate).

.EXAMPLE
  pwsh -NoProfile -File scripts/build/build.ps1

.EXAMPLE
  powershell -NoProfile -File scripts/build/build.ps1 -CheckOnly
#>
[CmdletBinding()]
param(
    [switch]$CheckOnly,
    [switch]$SkipPackage,
    [string]$StagingDir,
    [string]$AresCliDir,
    [switch]$AllowMissingApp,
    [switch]$Json
)

. (Join-Path $PSScriptRoot 'lib\Common.ps1')
. (Join-Path $PSScriptRoot 'lib\Checks.ps1')

$errors = New-Object System.Collections.Generic.List[object]
$warnings = New-Object System.Collections.Generic.List[object]
$infos = New-Object System.Collections.Generic.List[object]
$stages = New-Object System.Collections.Generic.List[object]
$criticalFailure = $null

function Expand-Issues {
    <# Normalises a single issue, an array of issues, or $null into an issue array. #>
    param([object]$Issues)
    $flat = @()
    foreach ($x in @($Issues)) {
        if ($null -eq $x) { continue }
        if ($x -is [System.Collections.IEnumerable] -and -not ($x -is [string])) { $flat += @($x) } else { $flat += $x }
    }
    return @($flat)
}

function Add-Results {
    param([object]$Issues, [ValidateSet('check', 'stage')][string]$Kind = 'check')
    foreach ($i in (Expand-Issues $Issues)) {
        if ($null -eq $i) { continue }
        switch ($i.severity) {
            'error' { $errors.Add($i) }
            'warn' { $warnings.Add($i) }
            default { $infos.Add($i) }
        }
    }
}

function Write-Issues {
    param([object]$Issues, [string]$Prefix = '')
    foreach ($i in @($Issues)) {
        if ($null -eq $i) { continue }
        $line = if ($i.path) { "$($i.path): $($i.message)" } else { $i.message }
        switch ($i.severity) {
            'error' { Write-Fail ("[$($i.id)] " + $line) }
            'warn' { Write-Warn ("[$($i.id)] " + $line) }
            default { Write-Host ("INFO  [$($i.id)] " + $line) -ForegroundColor DarkGray }
        }
    }
}

$manifestPath = Join-Path $PSScriptRoot 'package-manifest.json'
if (-not (Test-Path -LiteralPath $manifestPath)) { Exit-WithCode 2 "manifest missing: $manifestPath" }
$manifest = Get-Content -Raw -LiteralPath $manifestPath | ConvertFrom-Json

$appDir = Resolve-RelPath $manifest.appDir
$appReady = Test-Path -LiteralPath (Join-Path $appDir 'appinfo.json')
$staging = if ($StagingDir) { Resolve-RelPath $StagingDir } else { Join-Path (Get-DistDir) 'staging' }
$targetChromium = if ($manifest.targets.PSObject.Properties['targetChromium']) { [int]$manifest.targets.targetChromium } else { 87 }
$ares = Find-AresCli -AresCliDir $AresCliDir

Write-Step 'env' "repo=$(Get-RepoRoot)"
Write-Host "app:     $appDir  (appinfo: $(if ($appReady) { 'present' } else { 'MISSING' }))"
Write-Host "staging: $staging"
Write-Host "ares:    $(if ($ares) { $ares.PackageCmd } else { 'NOT FOUND' })"
Write-Host "target:  webOS $($manifest.targets.webos), Chromium $targetChromium floor; TV safe 1920x1080"

if (-not $appReady) {
    if (-not $AllowMissingApp) {
        Write-Fail "app/appinfo.json not found. The app/ scope (Flash-high) is not ready."
        Write-Host 'Scaffold check: run with -AllowMissingApp to validate the pipeline itself; this is not a release build.'
        Exit-WithCode 2 'BUILD BLOCKED: app/ not ready (dependency: Flash-high app).'
    }
    Write-Warn "-AllowMissingApp: app/ not ready. Running pipeline self-checks only (NOT a release artifact)."
}

# ---------------------------------------------------------------------------
# 1. Source-level checks (read-only)
# ---------------------------------------------------------------------------
$scannedApp = @()
if ($appReady) {
    Write-Step 'check' 'appinfo.json contract'
    $r = Test-Appinfo -AppDir $appDir
    Add-Results $r; Write-Issues $r
    $stage = [ordered]@{ name = 'appinfo'; ok = (@($r | Where-Object { $_.severity -eq 'error' })).Count -eq 0; errors = (@($r | Where-Object { $_.severity -eq 'error' })).Count; warnings = (@($r | Where-Object { $_.severity -eq 'warn' })).Count }
    $stages.Add($stage)
    if ($stage.ok) {
        $ai = Read-Appinfo -AppDir $appDir
        Write-Ok "appinfo: id=$($ai.Data.id) version=$($ai.Data.version) title=$($ai.Data.title) resolution=$($ai.Data.resolution)"
    }

    # app/ holds local preview/browser-profile artifacts (owned by Flash-high); none of them
    # are packaged (see manifest.exclude) and none are TV runtime sources, so the static scans
    # skip dot-entries.
    function Get-AppSourceFiles {
        param([string[]]$Include)
        $all = @(Get-ScannedFiles -Root $appDir -Include $Include)
        $kept = [System.Collections.Generic.List[System.IO.FileInfo]]::new()
        foreach ($item in $all) {
            $rel = Get-RelativeRepoPath $item.FullName
            $leaf = [System.IO.Path]::GetFileName($rel)
            if (-not $leaf.StartsWith('.') -and $rel -notmatch '(?i)(^|/)\.') { $kept.Add($item) }
        }
        return ,$kept.ToArray()
    }

    $scannedApp = Get-AppSourceFiles -Include @('*.html', '*.htm')
    foreach ($h in $scannedApp) {
        Write-Step 'check' "HTML $(Get-RelativeRepoPath $h.FullName)"
        $r = Test-HtmlStatic -HtmlPath $h.FullName -CheckRefs
        Add-Results $r; Write-Issues $r
        if (@($r | Where-Object { $_.severity -eq 'error' }).Count -eq 0) { Write-Ok 'HTML structure + local references' }
    }
    foreach ($c in (Get-AppSourceFiles -Include @('*.css'))) {
        $r = Test-CssBalance -CssPath $c.FullName
        Add-Results $r; Write-Issues $r
    }
    $js = @(Get-AppSourceFiles -Include @('*.js', '*.mjs', '*.cjs'))
    Write-Step 'check' "JavaScript syntax ($(@($js | Where-Object { $_.Extension -notmatch 'html?$' }).Count) files)"
    $r = Test-JsSyntax -Files $js
    Add-Results $r; Write-Issues $r
    if (@($r | Where-Object { $_.severity -eq 'error' }).Count -eq 0) { Write-Ok 'JavaScript parses under node --check' }
    $stages.Add([ordered]@{ name = 'js-syntax'; ok = (@($r | Where-Object { $_.severity -eq 'error' })).Count -eq 0; errors = (@($r | Where-Object { $_.severity -eq 'error' })).Count })

    Write-Step 'check' "TV static compatibility (Chromium >= $targetChromium)"
    $r = Test-TvStaticCompat -AppDir $appDir -TargetChromium $targetChromium
    Add-Results $r; Write-Issues $r
    if (@($r | Where-Object { $_.severity -eq 'error' }).Count -eq 0) { Write-Ok 'no JS/CSS features above the Chromium floor' }
    $stages.Add([ordered]@{ name = 'tv-static-compat'; ok = (@($r | Where-Object { $_.severity -eq 'error' })).Count -eq 0; errors = (@($r | Where-Object { $_.severity -eq 'error' })).Count; warnings = (@($r | Where-Object { $_.severity -eq 'warn' })).Count })

    Write-Step 'check' '1080p TV-safe layout heuristics'
    $r = Test-TvLayoutSafety -AppDir $appDir
    Add-Results $r; Write-Issues $r
}

Write-Step 'check' 'No legacy launcher coupling in the shipped app/dock'
$orbis = @(Test-NoLegacyLauncherCoupling -ScanRoots @('app', 'dock'))
Add-Results $orbis; Write-Issues $orbis
if ($orbis.Count -eq 0) { Write-Ok 'app/ and dock/ have no legacy launcher references' }
$stages.Add([ordered]@{ name = 'no-legacy-coupling'; ok = $orbis.Count -eq 0; errors = $orbis.Count })

Write-Step 'check' 'Forbidden artifacts (secrets/backups/IPKs/certs)'
$forbidden = @(Test-ForbiddenArtifacts)
Add-Results $forbidden; Write-Issues $forbidden
if ($forbidden.Count -eq 0) { Write-Ok 'none found' }

if ($errors.Count -gt 0) {
    Write-Fail "$($errors.Count) blocking error(s) before packaging."
    if (-not $Json) { Write-Host 'Packaging is skipped when static checks fail (first-failure policy).' }
    $criticalFailure = 'static checks failed'
}

# ---------------------------------------------------------------------------
# 2. Stage
# ---------------------------------------------------------------------------
function Get-StageExcludeMatchers {
    <# Compiles manifest exclude patterns (glob, /-separated) to regexes. A directory
       pattern like 'node_modules' also excludes everything below it. #>
    param([string[]]$Exclude)
    $regs = @()
    foreach ($raw in @($Exclude)) {
        if (-not $raw -or $raw.StartsWith('$')) { continue }
        $p = $raw.Trim() -replace '\\', '/'
        $rx = [regex]::Escape($p).Replace('\*\*/', '(?:.*/)?').Replace('\*\*', '.*').Replace('\*', '[^/]*').Replace('\?', '[^/]')
        $regs += [regex]::new('^' + $rx + '(?:/.*)?$', 'IgnoreCase')
    }
    return $regs
}

function Copy-StageTree {
    param([string]$Source, [string]$Destination, [string[]]$Exclude, [regex[]]$Matchers)
    $excludeRegs = if ($Matchers) { $Matchers } else { Get-StageExcludeMatchers -Exclude $Exclude }
    $sourceFull = [System.IO.Path]::GetFullPath($Source).TrimEnd('\', '/')
    $count = 0
    $bytes = 0
    New-Item -ItemType Directory -Path $Destination -Force | Out-Null
    # Flatten the recursive enumeration: wildcard-bearing names can yield nested arrays.
    $queue = [System.Collections.Generic.Queue[object]]::new()
    foreach ($x in @(Get-ChildItem -LiteralPath $Source -Recurse -File -Force)) { $queue.Enqueue($x) }
    $sourceFiles = [System.Collections.Generic.List[System.IO.FileInfo]]::new()
    while ($queue.Count -gt 0) {
        $x = $queue.Dequeue()
        if ($x -is [System.IO.FileInfo]) { $sourceFiles.Add($x) }
        elseif ($x -is [System.Collections.IEnumerable] -and -not ($x -is [string])) { foreach ($y in $x) { $queue.Enqueue($y) } }
    }
    foreach ($f in $sourceFiles) {
        $rel = [System.IO.Path]::GetFullPath($f.FullName).Substring($sourceFull.Length + 1).Replace('\', '/')
        $skip = $false
        foreach ($ex in $excludeRegs) {
            if ($ex.IsMatch($rel) -or $ex.IsMatch([System.IO.Path]::GetFileName($rel))) { $skip = $true; break }
        }
        if ($skip) { continue }
        # Never ship secret-ish or backup material, even if not excluded explicitly.
        $leaf = [System.IO.Path]::GetFileName($rel)
        if ($leaf -like '.env*' -and $leaf -notmatch '(?i)^\.env\.(example|sample|template)$') { continue }
        if ($leaf -match '(?i)\.bak($|\.)|^id_rsa|^id_ed25519|^_signature$') { continue }
        $dest = Join-Path $Destination ($rel -replace '/', '\')
        $destDir = [System.IO.Path]::GetDirectoryName($dest)
        if (-not (Test-Path -LiteralPath $destDir)) { New-Item -ItemType Directory -Path $destDir -Force | Out-Null }
        [System.IO.File]::Copy($f.FullName, $dest, $true)
        $count++
        $bytes += $f.Length
    }
    return [pscustomobject]@{ Files = $count; Bytes = $bytes }
}

$stageResult = $null
$packaged = $false
$ipkPath = $null
$ipkHash = $null
$sourceVersion = $null

if ($appReady) {
    $appinfo = Read-Appinfo -AppDir $appDir
    if ($appinfo.Data) { $sourceVersion = $appinfo.Data.version; $appId = $appinfo.Data.id }

    if (-not $criticalFailure) {
        Write-Step 'stage' "clean staging ($staging)"
        if (Test-Path -LiteralPath $staging) { Remove-Item -LiteralPath $staging -Recurse -Force }
        New-Item -ItemType Directory -Path $staging -Force | Out-Null
        $stageResult = Copy-StageTree -Source $appDir -Destination $staging -Exclude @($manifest.exclude)
        Write-Ok "staged $($stageResult.Files) files ($($stageResult.Bytes) bytes)"

        # Build-owned overrides. app/appinfo.json is owned by app/ and is never patched.
        # Validate override names before copying anything, so a refusal has no side effects.
        $refusedOverrides = @(foreach ($prop in $manifest.buildOverrides.PSObject.Properties) {
                $name = $prop.Name
                if ($name -match '(?i)^([\\/]|\.\.)|(^|[\\/])\.\.([\\/]|$)') { $name }
                elseif ($name -eq 'appinfo.json') { $name }
            })
        if ($refusedOverrides.Count -gt 0) {
            foreach ($bad in $refusedOverrides) {
                $id = if ($bad -eq 'appinfo.json') { 'stage.override-appinfo' } else { 'stage.override-escape' }
                $msg = if ($bad -eq 'appinfo.json') { 'refusing to override app/appinfo.json (owned by app/; ask Flash-high)' } else { 'override path escapes the app root' }
                Write-Fail "[$id] $bad : $msg"
                $errors.Add((New-Issue -Id $id -Severity 'error' -Path $bad -Message $msg))
            }
        }
        foreach ($prop in $manifest.buildOverrides.PSObject.Properties) {
            $relOverride = $prop.Name
            if ($relOverride -match '(?i)^(\.\.|[\\/])|(^|[\\/])\.\.([\\/]|$)') {
                $errors.Add((New-Issue -Id 'stage.override-escape' -Severity 'error' -Path $relOverride -Message 'override path escapes the app root'))
                continue
            }
            if ($relOverride -eq 'appinfo.json') {
                $errors.Add((New-Issue -Id 'stage.override-appinfo' -Severity 'error' -Path $relOverride -Message 'refusing to override app/appinfo.json (owned by app/; ask Flash-high)'))
                continue
            }
            $value = $prop.Value
            if ($value -is [string] -and $value -match '^<set-at-build>') { $value = $value.Replace('<set-at-build>', (Get-Date).ToUniversalTime().ToString('yyyy-MM-ddTHH:mm:ssZ')) }
            elseif ($value -is [string]) { $value = $value.Replace('<set-at-build>', (Get-Date).ToUniversalTime().ToString('yyyy-MM-ddTHH:mm:ssZ')) }
            elseif ($value -is [psobject] -and $value.PSObject.Properties['builtUtc']) {
                $obj = [ordered]@{}
                foreach ($p in $value.PSObject.Properties) {
                    if ($p.Name.StartsWith('$')) { continue }
                    $v = if ("$($p.Value)" -eq '<set-at-build>') { (Get-Date).ToUniversalTime().ToString('yyyy-MM-ddTHH:mm:ssZ') } else { $p.Value }
                    if ($p.Name -eq 'sourceVersion') { $v = $sourceVersion }
                    $obj[$p.Name] = $v
                }
                $value = $obj
            }
            $dest = Join-Path $staging ($relOverride -replace '/', '\')
            $dir = [System.IO.Path]::GetDirectoryName($dest)
            if (-not (Test-Path -LiteralPath $dir)) { New-Item -ItemType Directory -Path $dir -Force | Out-Null }
            if ($value -is [string]) { [System.IO.File]::WriteAllText($dest, $value, [System.Text.UTF8Encoding]::new($false)) }
            else { [System.IO.File]::WriteAllText($dest, ($value | ConvertTo-Json -Depth 6), [System.Text.UTF8Encoding]::new($false)) }
            Write-Ok "build override: $relOverride (build-owned, listed in build-info.json)"
        }

        # Post-stage assertions.
        foreach ($required in @('appinfo.json', 'index.html')) {
            if ($required -eq 'index.html' -and $appinfo.Data -and $appinfo.Data.main) { $required = $appinfo.Data.main }
            if (-not (Test-Path -LiteralPath (Join-Path $staging $required))) {
                $errors.Add((New-Issue -Id 'stage.required' -Severity 'error' -Path $required -Message "staged app is missing $required"))
            }
        }
        $leaks = @(Get-ChildItem -LiteralPath $staging -Recurse -File -Force | Where-Object { $_.Name -match '(?i)\.bak|^\.env($|\.)' -or $_.Extension -match '(?i)^\.(ipk|pem|key)$' })
        $leakCount = 0
        foreach ($l in $leaks) {
            if ($l -isnot [System.IO.FileInfo]) { continue }
            $leakCount++
            $errors.Add((New-Issue -Id 'stage.leak' -Severity 'error' -Path (Get-RepoRoot + '\dist\staging\' + $l.Name) -Message 'backup/secret/package file staged'))
        }
        # Scan the staged tree itself (not repo-relative paths: staging lives under dist/).
        $stagedOrbis = @(foreach ($sf in @(Get-ScannedFiles -Root $staging -Include '*.js', '*.mjs', '*.html', '*.css', '*.json', '*.py', '*.sh', '*.md')) {
                $st = Get-TextFileContent -Path $sf.FullName
                if ($st -and $st -match '(?i)orbis') { $sf.FullName.Substring($staging.Length + 1) }
            })
        foreach ($l in $stagedOrbis) { $errors.Add((New-Issue -Id 'orbis.reference' -Severity 'error' -Path ('staging:' + $l) -Message 'staged file mentions Orbis')) }
        if ($leakCount -eq 0 -and $stagedOrbis.Count -eq 0) { Write-Ok 'staged tree has no backup/secret files and no Orbis references' }
        $stages.Add([ordered]@{ name = 'staging'; ok = $true; files = $stageResult.Files; bytes = $stageResult.Bytes })
    }

    # -----------------------------------------------------------------------
    # 3. Package
    # -----------------------------------------------------------------------
    $doPackage = -not ($CheckOnly -or $SkipPackage)
    if ($doPackage -and -not $criticalFailure -and $errors.Count -eq 0) {
        if (-not $ares) {
            Write-Fail 'ares-cli not found. Expected beside the repo: B:\Dev\webos\.tools\ares-cli (or pass -AresCliDir/$env:ARES_CLI_DIR).'
            $criticalFailure = 'ares-cli missing'
            $errors.Add((New-Issue -Id 'package.ares-missing' -Severity 'error' -Path '' -Message 'ares-cli not found'))
        } else {
            $dist = Get-DistDir
            New-Item -ItemType Directory -Path $dist -Force | Out-Null
            $outName = "$($manifest.artifactName)-$sourceVersion"
            Write-Step 'package' "ares-package -> dist/$outName.ipk"
            $outTmp = Join-Path $dist '.pkg-out'
            if (Test-Path -LiteralPath $outTmp) { Remove-Item -LiteralPath $outTmp -Recurse -Force }
            New-Item -ItemType Directory -Path $outTmp -Force | Out-Null
            $r = Invoke-AresPackage -AresPackageCmd $ares.PackageCmd -Arguments @('-o', $outTmp, $staging)
            if ($r.StdOut.Trim()) { $r.StdOut.Trim() -split "`r?`n" | ForEach-Object { Write-Host "  ares> $_" } }
            if ($r.StdErr.Trim()) { $r.StdErr.Trim() -split "`r?`n" | ForEach-Object { Write-Warn "ares> $_" } }
            $built = @(Get-ChildItem -LiteralPath $outTmp -Filter '*.ipk' -File -ErrorAction SilentlyContinue)
            if ($r.ExitCode -ne 0 -or $built.Count -ne 1) {
                Write-Fail "ares-package failed (exit $($r.ExitCode), $($built.Count) ipk produced)"
                $errors.Add((New-Issue -Id 'package.ares' -Severity 'error' -Path '' -Message "ares-package exit $($r.ExitCode)"))
                $criticalFailure = 'ares-package failed'
            } else {
                $ipkPath = Join-Path $dist "$outName.ipk"
                if (Test-Path -LiteralPath $ipkPath) { Remove-Item -LiteralPath $ipkPath -Force }
                Move-Item -LiteralPath $built[0].FullName -Destination $ipkPath
                $packaged = $true
                $info = Invoke-AresPackage -AresPackageCmd $ares.PackageCmd -Arguments @('-i', $ipkPath)
                if ($info.StdOut.Trim()) { $info.StdOut.Trim() -split "`r?`n" | Where-Object { $_.Trim() } | Select-Object -First 40 | ForEach-Object { Write-Host "  info> $($_.Trim())" } }
                $ipkHash = (Get-FileHash -LiteralPath $ipkPath -Algorithm SHA256).Hash.ToLowerInvariant()
                $shaLine = "$ipkHash  $(Split-Path $ipkPath -Leaf)"
                [System.IO.File]::WriteAllText("$ipkPath.sha256", $shaLine + "`n", [System.Text.UTF8Encoding]::new($false))
                Write-Ok "IPK: $(Get-RelativeRepoPath $ipkPath) ($((Get-Item -LiteralPath $ipkPath).Length) bytes)"
                Write-Ok "sha256: $ipkHash"
                Write-Ok "sidecar: $(Split-Path $ipkPath -Leaf).sha256"
                $stages.Add([ordered]@{ name = 'package'; ok = $true; ipk = (Get-RelativeRepoPath $ipkPath); sha256 = $ipkHash })
            }
            Remove-Item -LiteralPath $outTmp -Recurse -Force -ErrorAction SilentlyContinue
        }
    } elseif ($doPackage) {
        Write-Skip 'packaging skipped because static checks failed'
    }
}

# ---------------------------------------------------------------------------
# 4. Receipt
# ---------------------------------------------------------------------------
$evidence = Get-RunEvidence -AresPackageCmd $(if ($ares) { $ares.PackageCmd } else { $null })
$info = [ordered]@{
    artifact         = if ($packaged) { (Get-RelativeRepoPath $ipkPath) } else { $null }
    sha256           = $ipkHash
    appId            = if ($appReady -and $appinfo.Data) { $appinfo.Data.id } else { $null }
    version          = $sourceVersion
    appReady         = [bool]$appReady
    checkOnly        = [bool]($CheckOnly -or $SkipPackage)
    allowMissingApp  = [bool]$AllowMissingApp
    status           = if ($errors.Count -gt 0) { 'FAIL' } elseif ($packaged) { 'PASS' } else { 'CHECKS_PASS_NO_ARTIFACT' }
    errors           = @(foreach ($e in $errors) { [ordered]@{ id = $e.id; path = $e.path; message = $e.message } })
    warnings         = @(foreach ($w in $warnings) { [ordered]@{ id = $w.id; path = $w.path; message = $w.message } })
    stages           = @(foreach ($s in $stages) { [ordered]@{ } + $s })
    buildOverrides   = @($manifest.buildOverrides.PSObject.Properties | ForEach-Object { $_.Name })
    exclusions       = @($manifest.exclude)
    targets          = $manifest.targets
    environment      = $evidence
}
$distDir = Get-DistDir
New-Item -ItemType Directory -Path $distDir -Force | Out-Null
$infoPath = Join-Path $distDir 'build-info.json'
# Serialise a plain snapshot: ConvertTo-Json cannot enumerate a Generic.List instance directly.
$infoJson = $info | ConvertTo-Json -Depth 8
[System.IO.File]::WriteAllText($infoPath, $infoJson, [System.Text.UTF8Encoding]::new($false))

Write-Step 'result' $(if ($errors.Count -gt 0) { 'FAIL' } elseif ($packaged) { 'PASS' } else { 'CHECKS PASS (no artifact)' })
Write-Host ("errors=$($errors.Count) warnings=$($warnings.Count) receipt=" + (Get-RelativeRepoPath $infoPath))
if ($packaged) { Write-Host ("artifact=" + (Get-RelativeRepoPath $ipkPath) + "  sha256=" + $ipkHash) }

if ($Json) { $infoJson }

if ($errors.Count -gt 0) {
    Exit-WithCode 1 'BUILD FAILED'
} elseif (-not $appReady) {
    Write-Warn 'Pipeline self-check OK, but no release artifact (app/ not ready).'
    Exit-WithCode 0
} else {
    Exit-WithCode 0 'BUILD OK'
}
