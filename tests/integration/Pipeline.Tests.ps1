# End-to-end pipeline checks.
#  1. Fixture proof: the real build.ps1 is run in an isolated mirror of scripts/build with
#     a synthetic app/ so staging, overrides, IPK creation, hashing and refusal rules are
#     exercised without touching the real app/ or dist/.
#  2. Real artifact: when the last build left dist/build-info.json + IPK, the IPK is
#     unpacked and inspected (files, appinfo, no secrets, no Orbis).
$root = Get-RepoRoot
$ares = Find-AresCli
$fixtureRoot = Join-Path ([System.IO.Path]::GetTempPath()) ('synergy-tv-int-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $fixtureRoot -Force | Out-Null
$map = [ordered]@{}

function Invoke-MirrorBuild {
    <# Copies scripts/build into an isolated tree (same relative depth) and runs build.ps1. #>
    param([hashtable]$ManifestOverride = @{}, [hashtable]$AppinfoOverride = @{}, [switch]$NoApp)
    $mirror = Join-Path $fixtureRoot ('m' + [guid]::NewGuid().ToString('N').Substring(0, 8))
    $mirrorBuild = Join-Path $mirror 'scripts\build'
    New-Item -ItemType Directory -Path $mirrorBuild -Force | Out-Null
    Copy-Item -Path (Join-Path $root 'scripts\build\*') -Destination $mirrorBuild -Recurse -Force
    $manifestPath = Join-Path $mirrorBuild 'package-manifest.json'
    if ($ManifestOverride.Count -gt 0) {
        $m = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
        foreach ($k in $ManifestOverride.Keys) { $m | Add-Member -MemberType NoteProperty -Name $k -Value $ManifestOverride[$k] -Force }
        [System.IO.File]::WriteAllText($manifestPath, ($m | ConvertTo-Json -Depth 8), [System.Text.UTF8Encoding]::new($false))
    }
    if (-not $NoApp) {
        $null = New-FixtureApp -Path (Join-Path $mirror 'app') -AppinfoOverride $AppinfoOverride
        New-Item -ItemType Directory -Force -Path (Join-Path $mirror 'app\client') | Out-Null
        [System.IO.File]::WriteAllText((Join-Path $mirror 'app\client\tvctl.sh'), "#!/bin/sh`necho snapshot`n")
    }
    $script = Join-Path $mirrorBuild 'build.ps1'
    $argList = @('-AresCliDir', $ares.CliDir)
    $r = Invoke-PowerShellScript -ScriptPath $script -Arguments $argList -TimeoutMs 300000
    return [pscustomobject]@{ ExitCode = $r.ExitCode; Output = ($r.Output + "`n" + $r.Error); Root = $mirror }
}

$map['pipeline fixture: real build.ps1 packages a synthetic app into an IPK with sha256'] = {
    if (-not $ares) { Block-Test 'ares-cli not found (B:\Dev\webos\.tools\ares-cli)' }
    $r = Invoke-MirrorBuild -AppinfoOverride @{ version = '9.9.9' }
    Assert-Equal 0 $r.ExitCode ("fixture build failed:`n" + $r.Output)
    $dist = Join-Path $r.Root 'dist'
    $ipk = @(Get-ChildItem -LiteralPath $dist -Filter '*.ipk' -File -ErrorAction SilentlyContinue)
    Assert-Equal 1 $ipk.Count ("expected one IPK, got " + $ipk.Count + "`n" + $r.Output)
    $shaFile = "$($ipk[0].FullName).sha256"
    Assert-Path $shaFile
    $expected = (Get-FileHash -LiteralPath $ipk[0].FullName -Algorithm SHA256).Hash.ToLowerInvariant()
    $recorded = ((Get-Content -LiteralPath $shaFile -Raw).Trim() -split '\s+')[0]
    Assert-Equal $expected $recorded 'sidecar hash matches the artifact'
    $info = Get-Content -LiteralPath (Join-Path $dist 'build-info.json') -Raw | ConvertFrom-Json
    Assert-Equal 'PASS' $info.status
    Assert-Equal $expected $info.sha256
    Assert-Equal '9.9.9' $info.version
    "ipk=$($ipk[0].Name) sha256=$expected"
}
$map['pipeline fixture: IPK contains the packaged app + build override, and no docs/caches'] = {
    if (-not $ares) { Block-Test 'ares-cli not found' }
    $r = Invoke-MirrorBuild
    if ($r.ExitCode -ne 0) { throw ("fixture build failed: " + $r.Output) }
    $ipk = @(Get-ChildItem -LiteralPath (Join-Path $r.Root 'dist') -Filter '*.ipk' -File)[0]
    $ipkFiles = Read-IpkFiles -IpkPath $ipk.FullName
    $names = @($ipkFiles.AppFiles.Keys | ForEach-Object { $_ -replace '^usr/palm/applications/[^/]+/', '' })
    Assert-True ($names -contains 'appinfo.json') 'appinfo.json packaged'
    Assert-True ($names -contains 'index.html') 'index.html packaged'
    Assert-True ($names -contains 'webos-meta/synergy-tv-build.json') 'build-owned override packaged'
    Assert-Equal 0 @($names | Where-Object { $_ -match '(?i)\.md$|\.bak|__pycache__|\.pyc$' }).Count 'no docs/backups/caches packaged'
    $info = [System.Text.Encoding]::UTF8.GetString($ipkFiles.AppFiles['usr/palm/applications/dev.hurrspice.fixture/appinfo.json']) | ConvertFrom-Json
    Assert-Equal 'dev.hurrspice.fixture' $info.id 'packaged appinfo is the app source, not a build override'
    'ipk contents ok: ' + ($names -join ', ')
}
$map['pipeline fixture: build refuses to override app/appinfo.json (exit 4 / error)'] = {
    if (-not $ares) { Block-Test 'ares-cli not found' }
    $r = Invoke-MirrorBuild -ManifestOverride @{ buildOverrides = @{ 'appinfo.json' = '{}' } }
    Assert-True ($r.ExitCode -ne 0) 'must fail when trying to override app-owned appinfo.json'
    Assert-Match $r.Output 'override-appinfo' 'explicit override refusal message'
    "exit=$($r.ExitCode)"
}
$map['pipeline fixture: -AllowMissingApp self-checks without producing an artifact'] = {
    $mirror = Join-Path $fixtureRoot ('m' + [guid]::NewGuid().ToString('N').Substring(0, 8))
    New-Item -ItemType Directory -Path (Join-Path $mirror 'scripts\build') -Force | Out-Null
    Copy-Item -Path (Join-Path $root 'scripts\build\*') -Destination (Join-Path $mirror 'scripts\build') -Recurse -Force
    $r = Invoke-PowerShellScript -ScriptPath (Join-Path $mirror 'scripts\build\build.ps1') -Arguments @('-AllowMissingApp', '-CheckOnly') -TimeoutMs 120000
    Assert-Equal 0 $r.ExitCode ($r.Output + "`n" + $r.Error)
    Assert-Equal 0 @(Get-ChildItem -LiteralPath (Join-Path $mirror 'dist') -Filter '*.ipk' -File -ErrorAction SilentlyContinue).Count 'scaffold mode must not emit an IPK'
    'scaffold mode ok'
}
$map['pipeline: real dist/ artifact (when built) is hashed and package-consistent'] = {
    $dist = Join-Path $root 'dist'
    if (-not (Test-Path -LiteralPath $dist)) { Skip-Test 'dist/ not present: run scripts/build/build.ps1' }
    $ipks = @(Get-ChildItem -LiteralPath $dist -Filter 'synergy-tv-app-*.ipk' -File -ErrorAction SilentlyContinue)
    if ($ipks.Count -eq 0) { Skip-Test 'no IPK in dist/: run scripts/build/build.ps1 (release build)' }
    $infoPath = Join-Path $dist 'build-info.json'
    Assert-Path $infoPath
    $info = Get-Content -LiteralPath $infoPath -Raw | ConvertFrom-Json
    Assert-Equal 'PASS' $info.status
    $ipk = $ipks | Sort-Object LastWriteTime -Descending | Select-Object -First 1
    $expected = (Get-FileHash -LiteralPath $ipk.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
    Assert-Equal $expected $info.sha256 'build-info hash matches the artifact'
    Assert-Path "$($ipk.FullName).sha256"
    $appInfoPath = Join-Path $root 'app\appinfo.json'
    if (Test-Path -LiteralPath $appInfoPath) {
        $ai = Get-Content -LiteralPath $appInfoPath -Raw | ConvertFrom-Json
        Assert-Equal $ai.version $info.version 'artifact was built from the current app version'
    }
    "$($ipk.Name) sha256=$expected"
}
$map['pipeline: release IPK payload matches the app contract (id, version, client, no debris)'] = {
    $ipks = @(Get-ChildItem -LiteralPath (Join-Path $root 'dist') -Filter 'synergy-tv-app-*.ipk' -File -ErrorAction SilentlyContinue)
    if ($ipks.Count -eq 0) { Skip-Test 'no release IPK yet: run scripts/build/build.ps1' }
    $ipk = $ipks | Sort-Object LastWriteTime -Descending | Select-Object -First 1
    $files = Read-IpkFiles -IpkPath $ipk.FullName
    $appRoots = @($files.AppFiles.Keys | Where-Object { $_ -match '/appinfo\.json$' } | ForEach-Object { $_ -replace '/appinfo\.json$', '' })
    Assert-Equal 1 $appRoots.Count 'exactly one packaged app'
    $rel = @(foreach ($k in $files.AppFiles.Keys) { $k -replace ('^' + [regex]::Escape($appRoots[0]) + '/'), '' })
    $ai = [System.Text.Encoding]::UTF8.GetString($files.AppFiles[$appRoots[0] + '/appinfo.json']) | ConvertFrom-Json
    Assert-Equal 'dev.canopy.synergywebos' $ai.id
    Assert-Equal 'web' $ai.type
    foreach ($required in 'index.html', 'js/core.js', 'js/app.js', 'css/app.css', 'client/client.py', 'client/tvctl.sh', 'client/probe.py', 'icon.png', 'icon-large.png') {
        Assert-True ($rel -contains $required) "IPK missing $required"
    }
    $debris = @($rel | Where-Object { $_ -match '(?i)\.md$|\.bak|__pycache__|\.pyc$|(^|/)\.|edge-profile|icon-512' })
    Assert-Equal 0 $debris.Count ("IPK contains non-runtime/debris files: " + ($debris -join ', '))
    Assert-True ($files.ControlFiles.ContainsKey('control')) 'IPK has a control file'
    $control = [System.Text.Encoding]::UTF8.GetString($files.ControlFiles['control'])
    Assert-Match $control ("(?m)^Version:\s*" + [regex]::Escape($ai.version)) 'control Version matches appinfo'
    Assert-Match $control '(?m)^Package:\s*dev\.canopy\.synergywebos' 'control Package matches appinfo id'
    Assert-Match $control '(?m)^Architecture:\s*all' 'architecture all (no TV CPU-specific binaries)'
    "$($ipk.Name): $($files.AppFiles.Count) files, id=$($ai.id) v=$($ai.version)"
}
$map['pipeline: no Orbis references or secrets inside the real packaged artifact'] = {
    $dist = Join-Path $root 'dist'
    $ipks = @(Get-ChildItem -LiteralPath $dist -Filter '*.ipk' -File -ErrorAction SilentlyContinue)
    if ($ipks.Count -eq 0) { Skip-Test 'no release IPK yet' }
    $staging = Join-Path $dist 'staging'
    if (-not (Test-Path -LiteralPath $staging)) { Skip-Test 'dist/staging cleaned; repackage to inspect contents' }
    $issues = @(Test-NoOrbisReferences -ScanRoots @($staging))
    Assert-NoIssues $issues | Out-Null
    $junk = @(Get-ChildItem -LiteralPath $staging -Recurse -File -Force | Where-Object { $_.Name -match '(?i)\.bak|^\.env|__pycache__|\.pyc$|edge-profile' })
    Assert-Equal 0 $junk.Count ("forbidden files staged: " + (($junk | Select-Object -First 5 | ForEach-Object Name) -join ', '))
    'artifact clean'
}

$map
