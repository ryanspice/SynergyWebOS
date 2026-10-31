# Unit tests for scripts/build/lib/Checks.ps1 and the pipeline contract.
# These are fixture-based and require no app/, dock/, or network. They double as
# sensitivity proofs: every fixture below is a known-bad input the check must catch.
$null = [System.Environment]::SetEnvironmentVariable('NO_COLOR', '1')

function Get-IssueIds { param($Issues, [string]$Severity = 'error') @($Issues | Where-Object { $_.severity -eq $Severity } | ForEach-Object { $_.id }) | Sort-Object -Unique }

$tmpRoot = Join-Path ([System.IO.Path]::GetTempPath()) ('synergy-tv-unit-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $tmpRoot -Force | Out-Null

$appinfo  = Join-Path $tmpRoot 'appinfo'
$html     = Join-Path $tmpRoot 'html'
$orbis    = Join-Path $tmpRoot 'orbis'
$tv       = Join-Path $tmpRoot 'tv'
$pkg      = Join-Path $tmpRoot 'pkg'
$homeDirFixtures = Join-Path $tmpRoot 'home'
$corner   = Join-Path $tmpRoot 'corner'
$stageSrc = Join-Path $tmpRoot 'stage-src'
$stageDst = Join-Path $tmpRoot 'stage-dst'
function New-FixtureDir {
    param([Parameter(Mandatory)][string]$Path)
    if (-not (Test-Path -LiteralPath $Path)) { New-Item -ItemType Directory -Path $Path -Force | Out-Null }
    return $Path
}
foreach ($d in $appinfo, $html, $orbis, $tv, $pkg, $homeDirFixtures, $corner, $stageSrc, $stageDst) { New-FixtureDir -Path $d | Out-Null }

$stageScript = Join-Path (Get-RepoRoot) 'scripts\build\build.ps1'
$stageAst = [System.Management.Automation.Language.Parser]::ParseFile($stageScript, [ref]$null, [ref]$null)
$copyFnAst = $stageAst.Find({ param($n) $n -is [System.Management.Automation.Language.FunctionDefinitionAst] -and $n.Name -eq 'Copy-StageTree' }, $true)
if (-not $copyFnAst) { throw 'Copy-StageTree not found in build.ps1; staging contract changed' }
$matcherFnAst = $stageAst.Find({ param($n) $n -is [System.Management.Automation.Language.FunctionDefinitionAst] -and $n.Name -eq 'Get-StageExcludeMatchers' }, $true)
if (-not $matcherFnAst) { throw 'Get-StageExcludeMatchers not found in build.ps1' }
# Run the production staging body and matcher directly. GetScriptBlock() keeps the param
# block and drops the comment-based help, which is itself output and would otherwise
# corrupt ScriptBlock::Create. Matchers are passed in, so no nested-function lookup.
$copyBody = $copyFnAst.Body.GetScriptBlock()
if ($copyBody -isnot [scriptblock]) { throw "Copy-StageTree extraction returned $($copyBody.GetType().Name), expected ScriptBlock" }
$script:CopyStage = $copyBody
$script:StageMatchers = [scriptblock]::Create($matcherFnAst.Body.GetScriptBlock().ToString())
$pkgInfo = Get-Content -LiteralPath (Join-Path (Get-RepoRoot) 'scripts\build\package-manifest.json') -Raw | ConvertFrom-Json

$map = [ordered]@{}

# --------------------------------------------------------------------------
$map['appinfo: valid fixture has no errors'] = {
    $dir = New-FixtureApp -Path (Join-Path $appinfo 'good')
    Assert-OnlyWarningsAbove (Test-Appinfo -AppDir $dir) | Out-Null
}
$map['appinfo: missing icon flagged'] = {
    $dir = New-FixtureApp -Path (Join-Path $appinfo 'noicon') -SkipIcon
    (Get-IssueIds (Test-Appinfo -AppDir $dir)) -contains 'appinfo.asset-missing' | ForEach-Object { Assert-True $_ 'missing icon not flagged' }
}
$map['appinfo: two-part version flagged'] = {
    $dir = New-FixtureApp -Path (Join-Path $appinfo 'badver') -AppinfoOverride @{ version = '1.2' }
    (Get-IssueIds (Test-Appinfo -AppDir $dir)) -contains 'appinfo.version' | ForEach-Object { Assert-True $_ }
}
$map['appinfo: reserved id flagged'] = {
    $dir = New-FixtureApp -Path (Join-Path $appinfo 'reserved') -AppinfoOverride @{ id = 'com.webos.synergy' }
    (Get-IssueIds (Test-Appinfo -AppDir $dir)) -contains 'appinfo.id' | ForEach-Object { Assert-True $_ }
}
$map['appinfo: bad resolution + non-array requiredACG flagged'] = {
    $dir = New-FixtureApp -Path (Join-Path $appinfo 'res2') -AppinfoOverride @{ resolution = '3840x2160'; requiredACG = 'application.launcher' }
    $ids = Get-IssueIds (Test-Appinfo -AppDir $dir)
    ($ids -contains 'appinfo.resolution') | ForEach-Object { Assert-True $_ 'resolution' }
    ($ids -contains 'appinfo.acg') | ForEach-Object { Assert-True $_ 'acg' }
}
$map['appinfo: invalid JSON flagged'] = {
    $dir = New-FixtureApp -Path (Join-Path $appinfo 'badjson')
    [System.IO.File]::WriteAllText((Join-Path $dir 'appinfo.json'), '{ "id": "x", ')
    (Get-IssueIds (Test-Appinfo -AppDir $dir)) -contains 'appinfo.parse' | ForEach-Object { Assert-True $_ }
}

# --------------------------------------------------------------------------
$map['html: valid fixture passes'] = {
    $dir = New-FixtureApp -Path (Join-Path $html 'good')
    Assert-OnlyWarningsAbove (Test-HtmlStatic -HtmlPath (Join-Path $dir 'index.html') -CheckRefs) | Out-Null
}
$map['html: missing referenced asset flagged'] = {
    $dir = New-FixtureApp -Path (Join-Path $html 'brokenref')
    Remove-Item (Join-Path $dir 'styles.css')
    (Get-IssueIds (Test-HtmlStatic -HtmlPath (Join-Path $dir 'index.html') -CheckRefs)) -contains 'html.missing-asset' | ForEach-Object { Assert-True $_ }
}
$map['html: unbalanced tags flagged'] = {
    $p = Join-Path $html 'unbalanced.html'
    [System.IO.File]::WriteAllText($p, "<!doctype html><html lang='en'><head><meta charset='utf-8'><title>x</title></head><body><main><div></main></body></html>")
    (Get-IssueIds (Test-HtmlStatic -HtmlPath $p)) -contains 'html.balance' | ForEach-Object { Assert-True $_ }
}
$map['html: inline script with html fragments stays balanced'] = {
    $p = Join-Path $html 'inline-script.html'
    $body = "<!doctype html><html lang='en'><head><meta charset='utf-8'><title>x</title></head><body><main><p id='a'></p></main><script>var s = '<div>'; if (1 < 2) { document.title = 'ok'; }</script></body></html>"
    [System.IO.File]::WriteAllText($p, $body)
    Assert-OnlyWarningsAbove (Test-HtmlStatic -HtmlPath $p) | Out-Null
}
$map['css: unbalanced braces flagged'] = {
    $p = Join-Path $html 'bad.css'
    [System.IO.File]::WriteAllText($p, 'body { color: red;')
    (Get-IssueIds (Test-CssBalance -CssPath $p)) -contains 'css.balance' | ForEach-Object { Assert-True $_ }
}

# --------------------------------------------------------------------------
$map['no-orbis: clean fixture passes'] = {
    $dir = New-FixtureApp -Path (Join-Path $orbis 'clean')
    Assert-NoIssues (Test-NoOrbisReferences -ScanRoots @($dir)) | Out-Null
}
$map['no-orbis: reference in a nested file is caught'] = {
    $dir = New-FixtureApp -Path (Join-Path $orbis 'dirty')
    New-Item -ItemType Directory -Path (Join-Path $dir 'src') -Force | Out-Null
    [System.IO.File]::WriteAllText((Join-Path $dir 'src\dock.js'), '// launch the Orbis Home app`n')
    (Get-IssueIds (Test-NoOrbisReferences -ScanRoots @($dir))) -contains 'orbis.reference' | ForEach-Object { Assert-True $_ }
}

# --------------------------------------------------------------------------
$map['tv-compat: modern JS rejected at Chromium 87 floor'] = {
    $dir = New-FixtureApp -Path (Join-Path $tv 'modern')
    [System.IO.File]::WriteAllText((Join-Path $dir 'app.js'), "const m = 'abc'.match(/(?<=a)b/);`n")
    (Get-IssueIds (Test-TvStaticCompat -AppDir $dir -TargetChromium 87)) -contains 'tv.regexp-lookbehind' | ForEach-Object { Assert-True $_ }
}
$map['tv-compat: static block + private class members rejected'] = {
    $dir = New-FixtureApp -Path (Join-Path $tv 'blocks')
    [System.IO.File]::WriteAllText((Join-Path $dir 'app.js'), "class A {`n  #x = 1`n  static {`n    A.y = 2`n  }`n}`n")
    $ids = Get-IssueIds (Test-TvStaticCompat -AppDir $dir -TargetChromium 87)
    ($ids -contains 'tv.class-private') | ForEach-Object { Assert-True $_ 'private' }
    ($ids -contains 'tv.static-blocks') | ForEach-Object { Assert-True $_ 'static block' }
}
$map['tv-compat: plain ES2017 code passes'] = {
    $dir = New-FixtureApp -Path (Join-Path $tv 'plain')
    $js = "const els = document.querySelectorAll('button');`nfor (const el of els) { el.addEventListener('click', function () { el.textContent = 'x'.split('').reverse().join(''); }); }`n"
    [System.IO.File]::WriteAllText((Join-Path $dir 'app.js'), $js)
    Assert-NoIssues (Test-TvStaticCompat -AppDir $dir -TargetChromium 87) | Out-Null
}
$map['tv-compat: remote script rejected'] = {
    $p = Join-Path $tv 'remote.html'
    [System.IO.File]::WriteAllText($p, "<!doctype html><html lang='en'><head><meta charset='utf-8'><title>x</title><script src='https://cdn.example/x.js'></script></head><body><main></main></body></html>")
    $dir = Split-Path $p
    New-Item -ItemType Directory -Path (Join-Path $dir 'remote') -Force | Out-Null
    Copy-Item $p (Join-Path $dir 'remote\index.html')
    (Get-IssueIds (Test-TvStaticCompat -AppDir (Join-Path $dir 'remote') -TargetChromium 87)) -contains 'tv.remote-script' | ForEach-Object { Assert-True $_ }
}
$map['js-syntax: valid parses, broken flagged'] = {
    $dir = New-FixtureApp -Path (Join-Path $tv 'syntax')
    Assert-NoIssues (Test-JsSyntax -Files @((Join-Path $dir 'app.js'))) | Out-Null
    [System.IO.File]::WriteAllText((Join-Path $dir 'bad.js'), 'function ( {')
    (Get-IssueIds (Test-JsSyntax -Files @((Join-Path $dir 'bad.js')))) -contains 'js.syntax' | ForEach-Object { Assert-True $_ }
}
$map['js-syntax: ESM parses'] = {
    $p = Join-Path $tv 'mod.mjs'
    [System.IO.File]::WriteAllText($p, "import { x } from './x.js';`nexport const y = x;`n")
    Assert-NoIssues (Test-JsSyntax -Files @($p)) | Out-Null
}
$map['py-syntax: valid parses, broken flagged'] = {
    $p = Join-Path $tv 'ok.py'
    $q = Join-Path $tv 'bad.py'
    [System.IO.File]::WriteAllText($p, "def f() -> int:`n    return 1`n")
    [System.IO.File]::WriteAllText($q, "def f(:`n")
    $issues = Test-PythonSyntax -Files @($p, $q)
    $ids = Get-IssueIds $issues
    if (($issues | Where-Object { $_.id -eq 'py.missing' })) { return 'python absent: skipped' }
    ($ids -contains 'py.syntax') | ForEach-Object { Assert-True $_ }
    (@($issues | Where-Object { $_.path -match 'ok\.py' }).Count -eq 0) | ForEach-Object { Assert-True $_ 'valid python flagged' }
}

# --------------------------------------------------------------------------
$map['corner geometry: 2px on 4K = 0.052% x 0.093%'] = {
    $g = Test-CornerGeometry -ScreenWidth 3840 -ScreenHeight 2160 -CornerPx 2
    Assert-Equal '0.052' $g.PctWidth
    Assert-Equal '0.093' $g.PctHeight
    Assert-Equal '2' $g.PxAtWidth
    Assert-Equal '2.01' $g.PxAtHeight 'rounding at 0.093% gives 2.0088px'
}
$map['corner tooling: 2px config contract detected in sample'] = {
    $p = Join-Path $corner 'sample.conf'
    [System.IO.File]::WriteAllText($p, "section: screens`n`tswitchDelay = 250`nsection: links`n`tup(0.052,2) = webos-tv(0,100)`n")
    Assert-NoIssues (Test-CornerTooling -Paths @($p)) | Out-Null
}
$map['corner tooling: broad edge link without delay flagged'] = {
    $p = Join-Path $corner 'broad.conf'
    # Real newlines/tab: a broad edge link still parses as a valid Synergy link line.
    $text = "section: screens`nsection: links`n`tup(0,15) = webos-tv(0,100)`n"
    [System.IO.File]::WriteAllText($p, $text)
    $issues = @(Test-CornerTooling -Paths @($p))
    $ids = Get-IssueIds $issues 'error'
    Assert-False ($ids -contains 'corner.link-format') 'a valid (broad) link line must not be a format error'
    $warns = Get-IssueIds $issues 'warn'
    Assert-True ($warns -contains 'corner.switch-delay') 'missing switchDelay must be flagged'
    Assert-True ($warns -contains 'corner.override') 'missing override setting must be flagged'
}
$map['corner tooling: real tools/synergy tree provides link + switchDelay + corner options'] = {
    $files = Get-CornerToolingFiles
    if ($files.Count -eq 0) { Skip-Test 'tools/synergy (Terra 1) not present yet' }
    $issues = @(Test-CornerTooling -Paths $files)
    Assert-NoIssues $issues | Out-Null
    $warns = Get-IssueIds $issues 'warn'
    Assert-False ($warns -contains 'corner.switch-delay') 'tools/synergy must document a configurable switchDelay'
    ((Get-RelativeRepoPath $files[0].FullName) -like 'tools/synergy/*') | ForEach-Object { Assert-True $_ }
}

# --------------------------------------------------------------------------
$map['home contract: system-HOME launcher passes'] = {
    $p = Join-Path $homeDirFixtures 'client_home.py'
    $code = @(
        'LUNA_SEND = "/usr/bin/luna-send"'
        'HOME_APP = "com.webos.app.home"'
        'HOME_TARGET = "system"'
        'OVERRIDE_DELAY_MS = 0'
        '# KEY_LEFTMETA is swallowed; repeat/up suppressed.'
    ) -join "`n"
    [System.IO.File]::WriteAllText($p, $code)
    Assert-NoIssues (Test-HomeContract -ClientPy $p) | Out-Null
}
$map['home contract: missing HOME action flagged'] = {
    $p = Join-Path $homeDirFixtures 'client_bad.py'
    [System.IO.File]::WriteAllText($p, "# KEY_LEFTMETA -> nothing`nPATH = '/tmp/other.sock'`n")
    $ids = Get-IssueIds (Test-HomeContract -ClientPy $p)
    ($ids -contains 'home.action') | ForEach-Object { Assert-True $_ }
}

# --------------------------------------------------------------------------
$map['staging: excludes + secret/backup suppression'] = {
    # Assignment would pass the fixture's return value off as this file's map.
    $null = New-FixtureApp -Path $stageSrc
    if (Test-Path $stageDst) { Remove-Item $stageDst -Recurse -Force }
    New-Item -ItemType Directory -Path $stageDst -Force | Out-Null
    New-FixtureDir -Path (Join-Path $stageSrc 'node_modules\x') | Out-Null
    New-FixtureDir -Path (Join-Path $stageSrc 'src\components') | Out-Null
    [System.IO.File]::WriteAllText((Join-Path $stageSrc 'README.md'), '# doc')
    [System.IO.File]::WriteAllText((Join-Path $stageSrc 'index.html.bak'), 'backup')
    [System.IO.File]::WriteAllText((Join-Path $stageSrc '.env'), 'SECRET=1')
    [System.IO.File]::WriteAllText((Join-Path $stageSrc '.env.example'), 'SECRET=')
    [System.IO.File]::WriteAllText((Join-Path $stageSrc 'node_modules\x\index.js'), 'x')
    [System.IO.File]::WriteAllText((Join-Path $stageSrc 'src\components\a.ts'), 'export const a = 1;')
    $matchers = & $script:StageMatchers -Exclude @($pkgInfo.exclude)
    & $script:CopyStage -Source $stageSrc -Destination $stageDst -Matchers $matchers | Out-Null
    Assert-NotPath (Join-Path $stageDst 'README.md') 'md excluded'
    Assert-NotPath (Join-Path $stageDst 'index.html.bak') 'bak excluded'
    Assert-NotPath (Join-Path $stageDst '.env') 'env suppressed'
    Assert-NotPath (Join-Path $stageDst 'node_modules') 'node_modules excluded'
    Assert-Path (Join-Path $stageDst 'index.html')
    Assert-Path (Join-Path $stageDst 'src\components\a.ts') 'source subdirs staged'
    Assert-Path (Join-Path $stageDst '.env.example') 'env example allowed'
}
$map['manifest: excludes own appinfo.json override with explicit error'] = {
    $m = Get-Content -LiteralPath (Join-Path (Get-RepoRoot) 'scripts\build\package-manifest.json') -Raw | ConvertFrom-Json
    $names = @($m.buildOverrides.PSObject.Properties | ForEach-Object { $_.Name })
    ($names -contains 'appinfo.json') | ForEach-Object { Assert-False $_ 'build.ps1 refuses to patch appinfo.json (owned by app/)' }
    Assert-True ($m.exclude -contains '*.bak') 'backups excluded'
    Assert-True ($m.exclude -contains '*.md') 'docs excluded from IPK'
}
$map['pipeline: build.ps1 and Run-Tests.ps1 parse'] = {
    foreach ($p in @((Join-Path (Get-RepoRoot) 'scripts\build\build.ps1'), (Join-Path (Get-RepoRoot) 'tests\lib\Run-Tests.ps1'), (Join-Path (Get-RepoRoot) 'scripts\build\lib\Common.ps1'), (Join-Path (Get-RepoRoot) 'scripts\build\lib\Checks.ps1'))) {
        $errs = $null
        [System.Management.Automation.Language.Parser]::ParseFile($p, [ref]$null, [ref]$errs) | Out-Null
        if ($errs.Count -gt 0) { throw ("parse errors in " + $p + ": " + (($errs | ForEach-Object { $_.Message }) -join '; ')) }
    }
    'all parse'
}

$map
