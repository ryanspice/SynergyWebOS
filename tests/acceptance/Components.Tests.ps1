# Cross-component acceptance: dock absence, Home shortcuts and desktop handoff.
$root = Get-RepoRoot
$map = [ordered]@{}
$homeArt = Get-HomeArtifacts

$map['bundle: standalone dock is absent and cannot be launched from the app'] = {
    Assert-False (Test-Path -LiteralPath (Join-Path $root 'dock')) 'no standalone dock directory'
    foreach ($path in 'app/js/core.js', 'app/js/app.js', 'app/index.html') {
        Assert-NotMatch (Get-TextFileContent (Join-Path $root $path)) '(?i)dev\.canopy\.tvdock|openDock|prepareDockCatalog|launchDock|open-dock|dockSlot' 'no removed companion integration'
    }
}

# --------------------------------------------------------------------------
# Home contract (Terra 3 patch and/or the app client)
# --------------------------------------------------------------------------
$map['home: scripts/home/client-home.patch defines system HOME with delay and no Orbis'] = {
    $patch = Join-Path $homeArt.HomeDir 'client-home.patch'
    if (-not (Test-Path -LiteralPath $patch)) { Skip-Test 'scripts/home/client-home.patch not present (folded into app/client per app/INTEGRATION.md)' }
    $issues = @(Test-HomeContract -ClientPy $patch)
    Assert-NoIssues $issues | Out-Null
    Assert-Match (Get-TextFileContent -Path $patch) '(?m)^\+.*--home-target' 'patch adds --home-target'
    'patch contract ok'
}
$map['home: cleanup script is reversible (plan/apply/rollback) and parses'] = {
    $cleanup = Get-ChildItem -LiteralPath $homeArt.HomeDir -File -Filter '*.py' -ErrorAction SilentlyContinue |
        Where-Object { $_.Name -match '(?i)clean' } | Select-Object -First 1
    if (-not $cleanup) { Skip-Test 'scripts/home cleanup script not present' }
    $text = Get-TextFileContent -Path $cleanup.FullName
    foreach ($m in 'plan', 'apply', 'rollback') { Assert-Match $text "(?i)$m" "cleanup supports $m" }
    $issues = @(Test-PythonSyntax -Files @($cleanup.FullName))
    if (($issues | Where-Object { $_.id -eq 'py.missing' })) { Skip-Test 'python not installed' }
    Assert-NoIssues $issues | Out-Null
    'cleanup ok'
}
$map['home: cleanup script does not stop/restart services and needs no launch'] = {
    $cleanup = Get-ChildItem -LiteralPath $homeArt.HomeDir -File -Filter '*.py' -ErrorAction SilentlyContinue |
        Where-Object { $_.Name -match '(?i)clean' } | Select-Object -First 1
    if (-not $cleanup) { Skip-Test 'cleanup script not present' }
    $text = Get-TextFileContent -Path $cleanup.FullName
    Assert-NotMatch $text '(?i)systemctl\s+(restart|stop|start)|killall|pkill' 'cleanup must not restart/stop services or processes'
    # The -n 1 launch command is only recorded in a receipt/plan string for manual undo of the
    # Input Hook binding; it must never be executed by the script itself.
    Assert-NotMatch $text '(?i)\bsubprocess\.\w+\(\s*\[?[^\n]*COMMAND|os\.system\(\s*COMMAND' 'cleanup must not execute app launches'
    'cleanup is non-destructive to services'
}

# --------------------------------------------------------------------------
# Hot-corner tooling (Terra 1)
# --------------------------------------------------------------------------
$map['corner: tools/synergy provides link config, switchDelay and override guidance'] = {
    $files = Get-CornerToolingFiles
    if ($files.Count -eq 0) { Block-Test 'tools/synergy/ missing (Terra 1 not ready)' }
    $issues = @(Test-CornerTooling -Paths $files)
    Assert-NoIssues $issues | Out-Null
    $warns = @($issues | Where-Object { $_.severity -eq 'warn' } | ForEach-Object { $_.id })
    Assert-False ($warns -contains 'corner.switch-delay') 'switchDelay documented'
    Assert-False ($warns -contains 'corner.override') 'delay override documented'
    'corner contract ok'
}
$map['corner: candidate uses 2px percentage intervals, not the broad 15% edge'] = {
    $conf = Join-Path $root 'tools\synergy\candidate.conf'
    if (-not (Test-Path -LiteralPath $conf)) { Block-Test 'tools/synergy/candidate.conf missing' }
    $text = Get-TextFileContent -Path $conf
    # Link lines are "  up(0,<interval>) = webos-tv(0,100)". Only the PC->TV edges matter;
    # the value on the right-hand side is a landing position (100 = far edge), not an interval.
    # For 3840x2160 a 2px edge interval is 0.0520833333% (vertical) / 0.0925925926% (horizontal).
    $tvEdges = [regex]::Matches($text, '(?im)^\s*(up|down|left|right)\(\s*0\s*,\s*([0-9.]+)\s*\)\s*=\s*webos-tv\s*\(')
    Assert-True ($tvEdges.Count -ge 1) 'at least one PC->TV edge is defined'
    $intervals = @()
    foreach ($m in $tvEdges) { $intervals += [pscustomobject]@{ Dir = $m.Groups[1].Value; Pct = [double]$m.Groups[2].Value } }
    foreach ($i in $intervals) {
        Assert-True ($i.Pct -le 0.1) ("$($i.Dir) interval $($i.Pct)% is broader than 2px (max ~0.093% of 2160 / 0.052% of 3840); the old broad edge was 15%")
        Assert-True ($i.Pct -gt 0) ("$($i.Dir) interval must be non-zero (Synergy rejects 0)")
    }
    if (-not ($intervals | Where-Object { $_.Dir -match '(?i)up|down' })) { throw 'no vertical (up/down) edge to the TV: the top-left 2px corner needs it' }
    # A left-edge link to the TV enters on the TV's right edge and causes an
    # immediate right-edge return; keep the live/candidate configs free of it.
    if ($text -match '(?im)^\s*left\(\s*0\s*,[^)]*\)\s*=\s*webos-tv') { throw 'left-edge link to the TV is a known insta-return bug' }
    Assert-Match $text '(?i)switchDelay' 'switch delay is configurable in the candidate'
    ('2px intervals: ' + (($intervals | ForEach-Object { "$($_.Dir)=$($_.Pct)%" }) -join ', '))
}
$map['corner: parser validator script parses and is isolated to a non-live port'] = {
    $v = Join-Path $root 'tools\synergy\validate-parser.ps1'
    if (-not (Test-Path -LiteralPath $v)) { Block-Test 'tools/synergy/validate-parser.ps1 missing' }
    $errs = $null
    [System.Management.Automation.Language.Parser]::ParseFile($v, [ref]$null, [ref]$errs) | Out-Null
    Assert-Equal 0 $errs.Count 'PowerShell parses'
    $text = Get-TextFileContent -Path $v
    Assert-Match $text '24999' 'uses the agreed isolated test port'
    Assert-NotMatch $text '(?i)2480[013]\b' 'must not use live Synergy ports 24800/24801/24803'
    'validator ok'
}

$map
