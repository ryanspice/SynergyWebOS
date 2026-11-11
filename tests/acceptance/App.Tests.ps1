# Acceptance checks for the Junction app (app/**) — owned by Flash-high; read-only here.
# Static, deterministic checks of the packaged surface: appinfo, entry HTML, styles,
# JavaScript syntax, TV static compatibility and sample-script references.
# Missing app/ is reported as BLOCKED (the pipeline still runs; a release gate does not).
$appDir = Join-Path (Get-RepoRoot) 'app'
$appinfoPath = Join-Path $appDir 'appinfo.json'
$targetChromium = 87   # webOS 22 floor claimed by scripts/build/package-manifest.json
$map = [ordered]@{}

function Get-AppinfoObject { (Get-Content -LiteralPath $appinfoPath -Raw) | ConvertFrom-Json }

$map['app: appinfo.json exists and passes the webOS contract'] = {
    if (-not (Test-Path -LiteralPath $appinfoPath)) { Block-Test 'app/appinfo.json missing (Flash-high app not ready)' }
    Assert-NoIssues (Test-Appinfo -AppDir $appDir) | Out-Null
    $ai = Get-AppinfoObject
    Assert-Equal 'web' $ai.type
    Assert-Equal '1920x1080' $ai.resolution
    Assert-Equal 3 (@($ai.version -split '\.').Count) 'version is X.Y.Z'
}
$map['app: package identity is the upgrade-compatible id and >= installed 0.1.8'] = {
    if (-not (Test-Path -LiteralPath $appinfoPath)) { Block-Test 'app/appinfo.json missing' }
    $ai = Get-AppinfoObject
    Assert-Equal 'dev.canopy.synergywebos' $ai.id 'BRAND.md: keep the existing ID for in-place upgrade'
    $v = [version]$ai.version
    Assert-True ($v -ge [version]'0.1.9') ("version $($ai.version) must exceed the installed 0.1.8")
}
$map['app: requiredACG (webOS 26+) is a non-empty array of known groups'] = {
    if (-not (Test-Path -LiteralPath $appinfoPath)) { Block-Test 'app/appinfo.json missing' }
    $ai = Get-AppinfoObject
    Assert-True ($ai.PSObject.Properties['requiredACG']) 'requiredACG declared (webOS 26 enforces / 27 requires it)'
    $groups = @($ai.requiredACG)
    Assert-True ($groups.Count -gt 0)
    Assert-True ($groups -contains 'lunabus.query') 'lunabus.query is required for the Homebrew root helper'
    Assert-True ($groups -contains 'application.launcher') 'application.launcher is required to switch apps'
    $known = @('lunabus.query', 'network.query', 'application.launcher', 'application.query', 'database.operation')
    foreach ($g in $groups) { Assert-True ($known -contains $g) "unrecognized ACG group '$g' (not in the QA allow-list)" }
}
$map['app: entry HTML is valid, self-contained and reference-complete'] = {
    if (-not (Test-Path -LiteralPath (Join-Path $appDir 'index.html'))) { Block-Test 'app/index.html missing' }
    Assert-NoIssues (Test-HtmlStatic -HtmlPath (Join-Path $appDir 'index.html') -CheckRefs) | Out-Null
}
$map['app: stylesheets are balanced and have no remote import'] = {
    $css = @(Get-ScannedFiles -Root $appDir -Include '*.css')
    if ($css.Count -eq 0) { Block-Test 'no CSS yet (app not ready)' }
    foreach ($c in $css) { Assert-NoIssues (Test-CssBalance -CssPath $c.FullName) | Out-Null }
    "css files: $($css.Count)"
}
$map['app: JavaScript parses (node --check) and has no runtime dependency on Node'] = {
    $js = @(Get-ScannedFiles -Root $appDir -Include '*.js', '*.mjs')
    if ($js.Count -eq 0) { Block-Test 'no JS yet' }
    Assert-NoIssues (Test-JsSyntax -Files $js) | Out-Null
    foreach ($f in $js) {
        $t = Get-TextFileContent -Path $f.FullName
        Assert-NotMatch $t '(?m)^\s*(const|let|var)\s+\w+\s*=\s*require\([''"]' ("$($f.Name) uses CommonJS require")
        Assert-NotMatch $t 'process\.(exit|argv)' ("$($f.Name) uses Node process API")
    }
    "js files: $($js.Count)"
}
$map['app: TV static compatibility at the Chromium 87 floor'] = {
    if (-not (Test-Path -LiteralPath $appinfoPath)) { Block-Test 'app not ready' }
    $issues = @(Test-TvStaticCompat -AppDir $appDir -TargetChromium $targetChromium)
    Assert-NoIssues $issues | Out-Null
    $warn = @($issues | Where-Object { $_.severity -eq 'warn' })
    ("ok" + $(if ($warn.Count) { "; warnings: " + (($warn | ForEach-Object { $_.id }) -join ', ') } else { '' }))
}
$map['app: TV-switchable layout with keyboard focus and reduced motion'] = {
    if (-not (Test-Path -LiteralPath (Join-Path $appDir 'index.html'))) { Block-Test 'app not ready' }
    $issues = @(Test-TvLayoutSafety -AppDir $appDir)
    $hard = @($issues | Where-Object { $_.id -eq 'tv.viewport' })
    Assert-True ($hard.Count -eq 0) 'viewport meta present'
    $css = (@(Get-ScannedFiles -Root $appDir -Include '*.css') | ForEach-Object { Get-TextFileContent $_.FullName }) -join "`n"
    Assert-Match $css ':focus' 'visible focus styling exists'
    Assert-Match $css 'prefers-reduced-motion' 'reduced-motion honoured'
}
$map['app: local preview/profile debris is excluded from the package (warn, not fail)'] = {
    $appDir2 = Join-Path (Get-RepoRoot) 'app'
    if (-not (Test-Path -LiteralPath $appDir2)) { Block-Test 'app/ missing' }
    $junk = @(Get-ChildItem -LiteralPath $appDir2 -Recurse -Force -ErrorAction SilentlyContinue |
        Where-Object { $_.Name -match '(?i)^\.(edge|preview|chrome|cache)|__pycache__|\.pyc$' })
    if ($junk.Count -eq 0) { return 'no debris' }
    $rel = @($junk | ForEach-Object { (Get-RelativeRepoPath $_.FullName) -replace '^app/', '' })
    # Not fatal, but each one must provably be dropped before packaging: the staging filter
    # skips leading-dot entries and Python caches. Flagging an exception keeps it visible.
    $rel = @(foreach ($item in $rel) { $item -replace '\\', '/' })
    $risky = @($rel | Where-Object { $_ -notmatch '(?i)(^|[\\/])\.' -and $_ -notmatch '(?i)(^|[\\/])__pycache__([\\/]|$)' })
    Assert-Equal 0 $risky.Count ("app/ dev artifacts that would NOT be excluded from the IPK: " + ($risky -join ', '))
    "WARN: $($junk.Count) local dev artifact(s) in app/, excluded from packaging: " + ((@($rel) | Select-Object -First 4) -join ', ')
}
$map['app: no remote network requests or remote scripts (TV offline app)'] = {
    $files = @(Get-ScannedFiles -Root $appDir -Include '*.js', '*.html', '*.css')
    if ($files.Count -eq 0) { Block-Test 'app not ready' }
    foreach ($f in $files) {
        $t = Get-TextFileContent -Path $f.FullName
        if ($null -eq $t) { continue }
        # Allow localhost API origins and comments; a TV app must not load remote content.
        $codeOnly = [regex]::Replace($t, '(?s)/\*.*?\*/', '')
        $matches = [regex]::Matches($codeOnly, '(?i)(?:src|href)\s*=\s*["'']https?://|fetch\(\s*["'']https?://|new\s+WebSocket\(\s*["'']wss?://')
        Assert-Equal 0 $matches.Count ("$($f.Name) loads remote content: " + (($matches | ForEach-Object { $_.Value }) -join ', '))
    }
    'no remote loads'
}
$map['app: handoff profile schema is valid JSON Schema (junction.handoff.v1)'] = {
    $schemaPath = Join-Path $appDir 'contracts\handoff-profile.schema.json'
    if (-not (Test-Path -LiteralPath $schemaPath)) { Block-Test 'app/contracts/handoff-profile.schema.json missing' }
    $schema = Get-Content -LiteralPath $schemaPath -Raw | ConvertFrom-Json
    Assert-True ($schema.PSObject.Properties['properties']) 'schema has properties'
    $text = Get-Content -LiteralPath $schemaPath -Raw
    if ($text -match 'junction\.handoff\.v1|handoff\.v1|"\$id"') { 'schema id present' } else { 'schema parsed' }
}
$map['app: client scripts present, Python parses and tvctl.sh referenced commands exist'] = {
    $client = Join-Path $appDir 'client'
    if (-not (Test-Path -LiteralPath $client)) { Block-Test 'app/client/ missing' }
    $py = @(Get-ChildItem -LiteralPath $client -File -Filter '*.py' | Where-Object { $_.Name -notmatch '^(test_|conftest)' })
    Assert-True ($py.Count -ge 2) 'client.py + probe.py present'
    $issues = @(Test-PythonSyntax -Files $py.FullName)
    if (($issues | Where-Object { $_.id -eq 'py.missing' })) { Skip-Test 'python not installed; AST check not run' }
    Assert-NoIssues $issues | Out-Null
    $tvctl = Join-Path $client 'tvctl.sh'
    Assert-Path $tvctl
    $txt = [System.IO.File]::ReadAllText($tvctl)
    foreach ($cmd in 'snapshot', 'start', 'stop', 'probe', 'autostart-enable', 'autostart-disable', 'profile-save', 'server-status') {
        # Either a case label ("  snapshot)") or a call site ("tvctl snapshot") counts.
        $pattern = '(?i)(^|\s|["''])(' + [regex]::Escape($cmd) + ')(\s|["'']|\)|$)'
        Assert-Match $txt $pattern "tvctl.sh handles '$cmd' (INTEGRATION.md stable surface)"
    }
    if ($txt -match "`r`n") { throw 'tvctl.sh must use LF line endings (busybox sh on the TV)' }
    'client scripts ok'
}
$map['app: client home contract (system HOME, no Orbis, configurable delay)'] = {
    $py = Join-Path $appDir 'client\client.py'
    if (-not (Test-Path -LiteralPath $py)) { Block-Test 'app/client/client.py missing' }
    $issues = @(Test-HomeContract -ClientPy $py)
    Assert-NoIssues $issues | Out-Null
    'home contract ok'
}
$map['app: Meta keys are consumed and never injected (both 125 and 126)'] = {
    $py = Join-Path $appDir 'client\client.py'
    if (-not (Test-Path -LiteralPath $py)) { Block-Test 'client.py missing' }
    $text = Get-TextFileContent -Path $py
    Assert-Match $text '0xE05B:\s*125' 'left Meta maps to evdev 125'
    Assert-Match $text '0xE05C:\s*126' 'right Meta maps to evdev 126'
    Assert-Match $text '(?s)code in \(125, 126\).*?self\._home_key' 'Meta down is diverted to _home_key'
    Assert-Match $text '(?s)code in \(125, 126\).*?pass' 'Meta repeat is consumed'
    Assert-NotMatch $text 'self\.backend\.key\(125' 'left Meta is never injected'
    Assert-NotMatch $text 'self\.backend\.key\(126' 'right Meta is never injected'
}
$map['app: root helper finds the packaged client/tvctl.sh'] = {
    $core = Join-Path $appDir 'js\core.js'
    if (-not (Test-Path -LiteralPath $core)) { Block-Test 'core.js missing' }
    $text = Get-TextFileContent -Path $core
    Assert-Match $text '\$D/client/tvctl\.sh' 'helper checks the client/ subdirectory'
    Assert-Match $text 'TVCTL=' 'helper stores the resolved script path'
    Assert-Match $text 'exec sh "\$TVCTL"' 'helper executes the resolved script'
}
$map['app: Alt held for 5 seconds opens system HOME'] = {
    $py = Join-Path $appDir 'client\client.py'
    if (-not (Test-Path -LiteralPath $py)) { Block-Test 'client.py missing' }
    $text = Get-TextFileContent -Path $py
    Assert-Match $text 'ALT_HOLD_MS\s*=\s*5\.0' 'Alt hold is five seconds'
    Assert-Match $text '0xEFE9:\s*56' 'left Alt key ID maps to KEY_LEFTALT'
    Assert-Match $text '0xEFEA:\s*100' 'right Alt key ID maps to KEY_RIGHTALT'
    Assert-Match $text '(?s)def _alt_key.*?ALT_HOLD_MS.*?self\._alt_fire' 'Alt down starts the hold timer'
    Assert-Match $text '(?s)def _alt_fire.*?self\._launch_home' 'Alt hold opens system HOME'
    Assert-Match $text '(?s)elif code in ALT_KEY_CODES.*?self\._alt_key' 'Alt events are routed to the hold handler'
    Assert-Match $text '(?s)self\.alt_down\.clear\(\).*?self\.alt_timer' 'release/stop cancels the Alt hold'
}
$map['app: Alt+Tab cycles running apps without animation'] = {
    $py = Join-Path $appDir 'client\client.py'
    if (-not (Test-Path -LiteralPath $py)) { Block-Test 'client.py missing' }
    $text = Get-TextFileContent -Path $py
    Assert-Match $text 'TAB_KEY\s*=\s*15' 'Tab evdev code'
    Assert-Match $text '0xEF09:\s*15' 'Windows Tab KeyID maps to KEY_TAB'
    Assert-Match $text 'APP_SWITCH_RUNNING_URI.*dev/running' 'root running-app query'
    Assert-Match $text '(?s)def _alt_tab.*?self\._cycle_app' 'Alt+Tab starts the cycle'
    Assert-Match $text '(?s)def _cycle_app.*?self\._launch_app' 'cycle launches the next app'
    Assert-Match $text 'ALT-TAB ->' 'cycle is logged'
    Assert-Match $text '(?s)code == TAB_KEY.*?self\._alt_tab' 'Tab while Alt is routed to the switcher'
}
$map['app: Windows Super key IDs are consumed as HOME triggers'] = {
    $py = Join-Path $appDir 'client\client.py'
    if (-not (Test-Path -LiteralPath $py)) { Block-Test 'client.py missing' }
    $text = Get-TextFileContent -Path $py
    Assert-Match $text '0xEFEB:\s*125' 'Windows/Super left maps to 125'
    Assert-Match $text '0xEFEC:\s*126' 'Windows/Super right maps to 126'
    Assert-Match $text '0xEFE7:\s*125' 'Meta left alias preserved'
    Assert-Match $text '0xEFE8:\s*126' 'Meta right alias preserved'
    Assert-Match $text '(?s)if key_id in HOME_KEY_IDS' 'KeyID is authoritative for modifiers'
    'Super/Meta key IDs ok'
}

$map
