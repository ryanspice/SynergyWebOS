# Check implementations shared by scripts/build/build.ps1 (build) and
# tests/lib/Run-Tests.ps1 (QA). Each function returns a list of issue objects:
#   @{ id = '...'; severity = 'error'|'warn'|'info'; path = 'rel/path'; message = '...' }
# No native tool is required; these are deterministic text/structure checks
# so they run on Windows, Linux CI and a bare checkout without installation.

if (-not (Get-Command Resolve-RelPath -ErrorAction SilentlyContinue)) {
    . (Join-Path $PSScriptRoot 'Common.ps1')
}

$script:TextExtensions = @('.js', '.mjs', '.cjs', '.ts', '.mts', '.cts', '.html', '.htm',
    '.css', '.json', '.py', '.sh', '.ps1', '.md', '.txt', '.svelte', '.svg', '.yaml', '.yml',
    '.tsx', '.jsx', '.scss', '.conf', '.toml', '.ini', '.gitignore', '.gitattributes', '.cfg')

# Files never treated as shipped sources even if they live in a scanned tree.
$script:NonSourceFilePattern = '(?i)(^|[\\/])(node_modules|\.git|__pycache__|\.pytest_cache|\.cache|dist|backups?|\.bak[^\\/]*|.*\.bak|\.env[^\\/]*)([\\/]|$)'

function New-Issue {
    param([string]$Id, [ValidateSet('error', 'warn', 'info')][string]$Severity, [string]$Path, [string]$Message)
    return [pscustomobject]@{ id = $Id; severity = $Severity; path = $Path; message = $Message }
}

function Get-TextFileContent {
    <# Reads UTF-8 text, returns $null for binary/NUL-containing content. #>
    param([Parameter(Mandatory)][string]$Path)
    try {
        $bytes = [System.IO.File]::ReadAllBytes($Path)
    } catch { return $null }
    if ($bytes -contains 0) { return $null }
    return [System.Text.Encoding]::UTF8.GetString($bytes)
}

function Get-ScannedFiles {
    <# Enumerates source-ish files below a root, skipping non-source/junk paths. #>
    param(
        [Parameter(Mandatory)][string]$Root,
        [string[]]$Include = @('*'),
        [switch]$IncludeBak
    )
    if (-not (Test-Path -LiteralPath $Root)) { @() }
    # Build the list explicitly: `+=` a single object would replace the array with that object
    # (e.g. a string path), which then explodes under StrictMode when $f.FullName is read.
    $files = [System.Collections.Generic.List[System.IO.FileInfo]]::new()
    # Wildcard includes can return a nested array from the recursive enumeration: flatten.
    $flat = [System.Collections.Generic.List[object]]::new()
    $queue = [System.Collections.Generic.Queue[object]]::new()
    foreach ($f in @(Get-ChildItem -LiteralPath $Root -Recurse -File -Force -ErrorAction SilentlyContinue)) { $queue.Enqueue($f) }
    while ($queue.Count -gt 0) {
        $x = $queue.Dequeue()
        if ($x -is [System.IO.FileInfo]) { $flat.Add($x) }
        elseif ($x -is [System.Collections.IEnumerable] -and -not ($x -is [string])) { foreach ($y in $x) { $queue.Enqueue($y) } }
    }
    foreach ($f in $flat) {
        foreach ($pattern in $Include) {
            if ($f.Name -like $pattern) { $files.Add($f); break }
        }
    }
    $rootFull = [System.IO.Path]::GetFullPath($Root).TrimEnd('\', '/')
    $seen = @{}
    $result = [System.Collections.Generic.List[System.IO.FileInfo]]::new()
    foreach ($f in $files) {
        if ($seen.ContainsKey($f.FullName)) { continue }
        $seen[$f.FullName] = $true
        $rel = [System.IO.Path]::GetFullPath($f.FullName).Substring($rootFull.Length).TrimStart('\', '/')
        if (-not $IncludeBak -and $rel -match $script:NonSourceFilePattern) { continue }
        if (-not $IncludeBak -and $f.Name -match '(?i)\.bak') { continue }
        if ($IncludeBak -and $rel -match '(?i)(^|[\\/])(node_modules|\.git)([\\/]|$)') { continue }
        if ($f.Extension -and $script:TextExtensions -notcontains $f.Extension.ToLowerInvariant()) {
            if (-not (($f.Name.StartsWith('.')) -or ($f.Extension -eq ''))) { continue }
        }
        $result.Add($f)
    }
    # Emit FileInfo items directly when called as a statement, and a single array when
    # assigned; never let the caller receive a nested array.
    $result.ToArray()
}

# ---------------------------------------------------------------------------
# Orbis / legacy-coupling scan
# ---------------------------------------------------------------------------
# An Orbis *reference* is the token "Orbis" in any casing inside source, docs or
# config. This QA scope (scripts/build/** and tests/**) has to quote the token to
# name the check, so that surface is excluded; everything else -- the shipped
# product, its docs and config -- must never mention it. The exclusion is path
# based and never applies to files staged into the IPK.

$script:OrbisScanExcludedPrefixes = @('scripts/build/', 'tests/', 'scripts/home/')
# Append-only record of reviewed exclusions. scripts/home/cleanup.py and REPORT.md must name
# the legacy launcher package to remove the old prototype and preserve user data; they never
# target it as a home screen (the client's home path is checked separately).

# ---------------------------------------------------------------------------
# Legacy launcher coupling in the PRODUCT (stricter than the Orbis token scan)
# ---------------------------------------------------------------------------
function Test-NoLegacyLauncherCoupling {
    <# The shipped TV app and dock must never reference the old launcher package, its
       home hook, or system UI launchers. tools/ and scripts/home/ may (read-only research /
       cleanup target); those are excluded. #>
    param([string[]]$ScanRoots = @('app', 'dock'))
    $issues = @()
    $productScope = @(
        '(?i)com\.canopy\.',
        '(?i)orbis',
        '(?i)home-key\.py'
    )
    foreach ($root in $ScanRoots) {
        $abs = Resolve-RelPath $root
        if (-not (Test-Path -LiteralPath $abs)) { continue }
        $files = if ((Get-Item -LiteralPath $abs).PSIsContainer) { @(Get-ScannedFiles -Root $abs) } else { @(Get-Item -LiteralPath $abs) }
        foreach ($f in $files) {
            $rel = Get-RelativeRepoPath $f.FullName
            if (Test-OrbisScanExcluded $rel) { continue }
            $text = Get-TextFileContent -Path $f.FullName
            if ($null -eq $text) { continue }
            # A dock launch of its own app is fine; a launch of the legacy launcher is not.
            foreach ($p in $productScope) {
                if ($text -match $p) {
                    $m = [regex]::Match($text, $p)
                    $issues += New-Issue -Id 'legacy.launcher-coupling' -Severity 'error' -Path $rel -Message ("references '" + $m.Value + "' (legacy launcher coupling)")
                    break
                }
            }
        }
    }
    $issues
}

function Test-OrbisScanExcluded {
    param([string]$RelativePath)
    $p = ($RelativePath -replace '\\', '/').TrimStart('/')
    foreach ($prefix in $script:OrbisScanExcludedPrefixes) {
        if ($p.StartsWith($prefix, [System.StringComparison]::OrdinalIgnoreCase)) { return $true }
    }
    return $false
}
function Get-OrbisReferenceFiles {
    <# Returns rel paths that mention Orbis. ScanRoots default to the whole repo (excluding .git/dist/report files). #>
    param(
        [string[]]$ScanRoots,
        [string[]]$ExcludeRelative = @()
    )
    if (-not $ScanRoots) {
        $ScanRoots = @(
            'app', 'dock', 'tools', 'scripts', 'tests',
            'README.md', 'LICENSE', 'CONTRIBUTING.md', 'SECURITY.md', 'package.json', '.gitignore'
        )
    }
    $hits = @()
    foreach ($root in $ScanRoots) {
        $abs = Resolve-RelPath $root
        if (-not (Test-Path -LiteralPath $abs)) { continue }
        if ((Get-Item -LiteralPath $abs).PSIsContainer) { $files = @(Get-ScannedFiles -Root $abs) }
        else { $files = @(Get-Item -LiteralPath $abs) }
        foreach ($f in $files) {
            $rel = Get-RelativeRepoPath $f.FullName
            if (Test-OrbisScanExcluded $rel) { continue }
            if ($ExcludeRelative -contains $rel) { continue }
            $text = Get-TextFileContent -Path $f.FullName
            if ($null -eq $text) { continue }
            if ($text -match '(?i)orbis') {
                $lines = @()
                $n = 0
                foreach ($line in ($text -split "`r?`n")) {
                    $n++
                    if ($line -match '(?i)orbis') { $lines += "${n}:" + $line.Trim() }
                }
                $hits += [pscustomobject]@{ Path = $rel; Lines = ($lines | Select-Object -First 5) }
            }
        }
    }
    return @($hits)
}

function Test-NoOrbisReferences {
    param([string[]]$ScanRoots)
    $hits = Get-OrbisReferenceFiles -ScanRoots $ScanRoots
    $issues = @()
    foreach ($h in $hits) {
        $issues += New-Issue -Id 'orbis.reference' -Severity 'error' -Path $h.Path -Message ("mentions Orbis: " + ($h.Lines -join ' | '))
    }
    $issues
}

function Test-ForbiddenArtifacts {
    <# Secrets, backups, proprietary Synergy bundles, certs, env files anywhere in the repo. #>
    param()
    $issues = @()
    $root = Get-RepoRoot
    $bad = Get-ChildItem -LiteralPath $root -Recurse -File -Force -ErrorAction SilentlyContinue |
        Where-Object { $_.FullName -notmatch '(?i)([\\/])(\.git|node_modules|dist)([\\/]|$)' }
    foreach ($f in $bad) {
        $rel = Get-RelativeRepoPath $f.FullName
        $name = $f.Name
        $hit = $null
        if ($name -match '(?i)\.bak') { $hit = 'backup .bak file' }
        elseif ($name -match '(?i)^\.env' -and $name -notmatch '(?i)\.example$|\.sample$|\.template$') { $hit = 'environment file' }
        elseif ($f.Extension -match '(?i)^\.(pem|key|p12|pfx|jks|keystore)$') { $hit = 'credential material' }
        elseif ($f.Extension -match '(?i)^\.(ipk|wgt)$') { $hit = 'package artifact (IPK should live in gitignored dist/ or a release)' }
        elseif ($name -match '(?i)^id_rsa|^id_ed25519|^_signature') { $hit = 'private key' }
        elseif ($name -match '(?i)^(synergy-core|synergy-service|synergy-?client)(\.exe)?$') { $hit = 'proprietary Synergy binary' }
        if ($hit) { $issues += New-Issue -Id 'repo.forbidden-artifact' -Severity 'error' -Path $rel -Message $hit }
    }
    $issues
}

# ---------------------------------------------------------------------------
# appinfo.json / webOS package contract
# ---------------------------------------------------------------------------
function Read-Appinfo {
    param([Parameter(Mandatory)][string]$AppDir)
    $path = Join-Path $AppDir 'appinfo.json'
    if (-not (Test-Path -LiteralPath $path)) { return [pscustomobject]@{ Path = $path; Data = $null; Error = 'appinfo.json missing' } }
    try {
        $raw = [System.IO.File]::ReadAllText($path)
        $data = $raw | ConvertFrom-Json
        return [pscustomobject]@{ Path = $path; Data = $data; Error = $null; Raw = $raw }
    } catch {
        return [pscustomobject]@{ Path = $path; Data = $null; Error = ("invalid JSON: " + $_.Exception.Message) }
    }
}

function Test-Appinfo {
    param([Parameter(Mandatory)][string]$AppDir)
    $issues = @()
    $info = Read-Appinfo -AppDir $AppDir
    $rel = Get-RelativeRepoPath $info.Path
    if ($info.Error) { return @(New-Issue -Id 'appinfo.parse' -Severity 'error' -Path $rel -Message $info.Error) }
    $a = $info.Data

    foreach ($key in 'id', 'title', 'type', 'main', 'icon', 'version') {
        $prop = $a.PSObject.Properties[$key]
        if (-not $prop -or -not "$($prop.Value)".Trim()) {
            $issues += New-Issue -Id 'appinfo.required' -Severity 'error' -Path $rel -Message "missing required field '$key'"
        }
    }
    if ($a.id -and $a.id -notmatch '^[a-z0-9.+-]+$') { $issues += New-Issue -Id 'appinfo.id' -Severity 'error' -Path $rel -Message "id '$($a.id)' must be lowercase letters/digits/./-" }
    if ($a.id -and $a.id -match '^(com\.(palm|webos|lge)|com\.palmdts)\.') { $issues += New-Issue -Id 'appinfo.id' -Severity 'error' -Path $rel -Message "id '$($a.id)' uses a reserved domain" }
    if ($a.version -and $a.version -notmatch '^\d{1,9}\.\d{1,9}\.\d{1,9}$') { $issues += New-Issue -Id 'appinfo.version' -Severity 'error' -Path $rel -Message "version '$($a.version)' must be three non-negative integers (X.Y.Z)" }
    if ($a.type -and $a.type -ne 'web') { $issues += New-Issue -Id 'appinfo.type' -Severity 'error' -Path $rel -Message "only type 'web' is supported, got '$($a.type)'" }
    # resolution is optional on webOS (default 1920x1080); the dock omits it deliberately.
    $resolutionProp = $a.PSObject.Properties['resolution']
    if (-not $resolutionProp) {
        $issues += New-Issue -Id 'appinfo.resolution' -Severity 'info' -Path $rel -Message 'no resolution field; webOS defaults to 1920x1080 (declare it on the TV app)'
    } elseif (@('1920x1080', '1280x720') -notcontains "$($resolutionProp.Value)") {
        $issues += New-Issue -Id 'appinfo.resolution' -Severity 'error' -Path $rel -Message "resolution '$($resolutionProp.Value)' unsupported (1920x1080|1280x720)"
    }
    if ($a.PSObject.Properties['requiredACG'] -and -not ($a.requiredACG -is [array])) { $issues += New-Issue -Id 'appinfo.acg' -Severity 'error' -Path $rel -Message 'requiredACG must be an array of ACG group names' }

    # Referenced assets must exist: ares-package fails hard on a missing icon/largeIcon/main.
    $assetKeys = 'main', 'icon', 'largeIcon', 'bgImage', 'splashBackground', 'imageForRecents'
    foreach ($key in $assetKeys) {
        $prop = $a.PSObject.Properties[$key]
        if ($prop -and "$($prop.Value)") {
            $target = Join-Path $AppDir ($prop.Value -replace '/', '\')
            if (-not (Test-Path -LiteralPath $target)) {
                $sev = if ($key -eq 'largeIcon' -or $key -eq 'bgImage') { 'warn' } else { 'error' }
                $issues += New-Issue -Id 'appinfo.asset-missing' -Severity $sev -Path $rel -Message "$key -> '$($prop.Value)' not found in app/"
            }
        }
    }
    if ($a.main -and $a.main -notmatch '(?i)\.html?$') { $issues += New-Issue -Id 'appinfo.main' -Severity 'error' -Path $rel -Message "main '$($a.main)' must be an .html entry point" }
    if ($a.PSObject.Properties['exclude'] -and $a.exclude -is [array]) {
        foreach ($ex in $a.exclude) {
            if ("$ex" -match '(?i)node_modules|\.git' -or "$ex" -match '^\*\.\*$') {
                $issues += New-Issue -Id 'appinfo.exclude' -Severity 'warn' -Path $rel -Message "broad exclude '$ex'"
            }
        }
    }
    $issues
}

function Get-AppinfoAcgGroups {
    <# Returns requiredACG groups declared in app/appinfo.json (empty when none). #>
    param([Parameter(Mandatory)][string]$AppDir)
    $info = Read-Appinfo -AppDir $AppDir
    if ($info.Error -or -not $info.Data) { @() }
    if ($info.Data.PSObject.Properties['requiredACG']) { return @($info.Data.requiredACG | ForEach-Object { "$_" }) }
    @()
}

# ---------------------------------------------------------------------------
# Node/browser static JavaScript syntax check
# ---------------------------------------------------------------------------
function Test-JsSyntax {
    <# node --check per file (ESM when .mjs or import/export present). Returns issues. #>
    param([string[]]$Files, [int]$TimeoutSeconds = 30)
    $issues = @()
    $node = Get-Command node -ErrorAction SilentlyContinue
    if (-not $node) {
        return @(New-Issue -Id 'js.node-missing' -Severity 'warn' -Path '' -Message 'node not found; JavaScript syntax check skipped')
    }
    $tmp = Join-Path ([System.IO.Path]::GetTempPath()) ('synergy-tv-jscheck-' + [guid]::NewGuid().ToString('N'))
    New-Item -ItemType Directory -Path $tmp -Force | Out-Null
    try {
        foreach ($item in @($Files)) {
            # Accept FileInfo objects or path strings (a single-element array may arrive unrolled).
            $f = if ($item -is [System.IO.FileSystemInfo]) { $item.FullName } else { [string]$item }
            if (-not $f -or -not (Test-Path -LiteralPath $f)) { continue }
            $rel = Get-RelativeRepoPath $f
            $ext = [System.IO.Path]::GetExtension($f).ToLowerInvariant()
            if (@('.ts', '.mts', '.cts', '.tsx') -contains $ext) { continue }  # TypeScript needs tsc; handled by typecheck
            $text = [System.IO.File]::ReadAllText($f)
            $isModule = ($ext -in '.mjs', '.mts') -or ($text -match '(?m)^\s*(import|export)\b')
            $copy = Join-Path $tmp ((('x' + ([guid]::NewGuid().ToString('N'))) + $(if ($isModule) { '.mjs' } else { '.js' })))
            [System.IO.File]::WriteAllText($copy, $text, [System.Text.UTF8Encoding]::new($false))
            $r = Invoke-Native -FilePath $node.Source -Arguments @('--check', $copy) -TimeoutMs ($TimeoutSeconds * 1000)
            if ($r.ExitCode -ne 0) {
                $msg = (($r.Error + "`n" + $r.Output) -split "`r?`n" | Where-Object { $_.Trim() } | Select-Object -First 4) -join ' | '
                $issues += New-Issue -Id 'js.syntax' -Severity 'error' -Path $rel -Message "node --check: $msg"
            }
        }
    } finally {
        Remove-Item -LiteralPath $tmp -Recurse -Force -ErrorAction SilentlyContinue
    }
    $issues
}

# ---------------------------------------------------------------------------
# Python syntax / byte-compile check (client patch)
# ---------------------------------------------------------------------------
function Test-PythonSyntax {
    param([string[]]$Files, [int]$TimeoutSeconds = 60)
    $issues = @()
    $python = Get-Command python -ErrorAction SilentlyContinue
    if (-not $python) { $python = Get-Command py -ErrorAction SilentlyContinue }
    if (-not $python) {
        return @(New-Issue -Id 'py.missing' -Severity 'warn' -Path '' -Message 'python not found; Python syntax/AST check skipped')
    }
    $pyArgsBase = @()
    if ($python.Name -eq 'py') { $pyArgsBase = @('-3') }
    $module = @'
import ast, json, sys
results = []
for path in sys.argv[1:]:
    try:
        with open(path, 'rb') as fh:
            src = fh.read()
        tree = ast.parse(src, filename=path)
        results.append({"path": path, "ok": True, "nodes": sum(1 for _ in ast.walk(tree))})
    except SyntaxError as exc:
        results.append({"path": path, "ok": False, "error": "%s:%s: %s" % (exc.filename, exc.lineno, exc.msg)})
    except Exception as exc:  # pragma: no cover - defensive
        results.append({"path": path, "ok": False, "error": "%s: %s" % (type(exc).__name__, exc)})
print(json.dumps(results))
'@
    $tmp = Join-Path ([System.IO.Path]::GetTempPath()) ('synergy-tv-pychk-' + [guid]::NewGuid().ToString('N') + '.py')
    [System.IO.File]::WriteAllText($tmp, $module, [System.Text.UTF8Encoding]::new($false))
    $paths = @(foreach ($item in @($Files)) {
            $p = if ($item -is [System.IO.FileSystemInfo]) { $item.FullName } else { "$item" }
            if ($p -and (Test-Path -LiteralPath $p)) { $p }
        })
    if ($paths.Count -eq 0) { Remove-Item -LiteralPath $tmp -Force -ErrorAction SilentlyContinue; @() }
    try {
        $r = Invoke-Native -FilePath $python.Source -Arguments ($pyArgsBase + @($tmp) + $paths) -TimeoutMs ($TimeoutSeconds * 1000)
        $payload = $null
        foreach ($line in ($r.Output -split "`r?`n")) {
            $t = $line.Trim()
            if ($t.StartsWith('[')) { $payload = $t | ConvertFrom-Json }
        }
        if (-not $payload) {
            return @(New-Issue -Id 'py.runner' -Severity 'error' -Path '' -Message ("python AST check did not run: " + (($r.Error + $r.Output) -split "`r?`n" | Select-Object -First 3) + ''))
        }
        foreach ($res in $payload) {
            if (-not $res.ok) {
                $issues += New-Issue -Id 'py.syntax' -Severity 'error' -Path (Get-RelativeRepoPath $res.path) -Message $res.error
            }
        }
    } finally {
        Remove-Item -LiteralPath $tmp -Force -ErrorAction SilentlyContinue
    }
    $issues
}

# ---------------------------------------------------------------------------
# HTML static validation (no external tool; ~HTML5-lite, webOS/TV relevant)
# ---------------------------------------------------------------------------
$script:VoidElements = @('area', 'base', 'br', 'col', 'embed', 'hr', 'img', 'input', 'link',
    'meta', 'param', 'source', 'track', 'wbr')

function Get-HtmlLocalRefs {
    <# Returns local asset references (href/src/srcset url()) from HTML, ignoring anchors/data:/http. #>
    param([Parameter(Mandatory)][string]$Text)
    $refs = @()
    $stripComments = [regex]::Replace($Text, '(?s)<!--.*?-->', '')
    foreach ($m in [regex]::Matches($stripComments, '(?i)\b(?:src|href)\s*=\s*(?:"([^"]*)"|''([^'']*)'')')) {
        $val = if ($m.Groups[1].Success) { $m.Groups[1].Value } else { $m.Groups[2].Value }
        $refs += $val
    }
    return @($refs)
}

function Test-HtmlStatic {
    <# Validates HTML structure + that local refs resolve + flags webOS-unsafe constructs. #>
    param([Parameter(Mandatory)][string]$HtmlPath, [switch]$CheckRefs)
    $issues = @()
    $rel = Get-RelativeRepoPath $HtmlPath
    $text = Get-TextFileContent -Path $HtmlPath
    if ($null -eq $text) { return @(New-Issue -Id 'html.read' -Severity 'error' -Path $rel -Message 'unreadable') }
    $noComments = [regex]::Replace($text, '(?s)<!--.*?-->', '')

    if ($noComments -notmatch '(?i)^\s*<!doctype\s+html\s*>') {
        $issues += New-Issue -Id 'html.doctype' -Severity 'warn' -Path $rel -Message 'missing <!DOCTYPE html> (quirks mode in Chromium)'
    }
    if ($noComments -notmatch '(?i)<meta\s+charset\s*=') {
        $issues += New-Issue -Id 'html.charset' -Severity 'warn' -Path $rel -Message 'missing <meta charset> (webOS file:// pages need explicit UTF-8)'
    }
    if ($noComments -notmatch '(?i)<html\b[^>]*\blang\s*=') {
        $issues += New-Issue -Id 'html.lang' -Severity 'warn' -Path $rel -Message 'missing <html lang> (accessibility/audio guidance)'
    }
    if ($noComments -notmatch '(?i)<title\b[^>]*>\s*\S') {
        $issues += New-Issue -Id 'html.title' -Severity 'warn' -Path $rel -Message 'missing non-empty <title>'
    }
    if ($noComments -notmatch '(?i)<main\b') {
        $issues += New-Issue -Id 'html.main' -Severity 'info' -Path $rel -Message 'no <main> landmark'
    }

    # Tag balance for non-void elements.
    $void = $script:VoidElements
    $stack = New-Object System.Collections.Generic.Stack[object]
    # Blank out raw-text element contents so JS/CSS "<" characters are not parsed as tags.
    $structure = [regex]::Replace($noComments, '(?is)<(script|style)\b([^>]*)>.*?</\1\s*>', '<$1$2></$1>')
    # SVG/MathML children are well-formed XML. Validate each block with the XML parser
    # (self-closing <path/> would otherwise trip the HTML void-element rule) and remove it
    # from the HTML tag-balance pass.
    foreach ($ns in 'svg', 'math') {
        $pattern = '(?is)<' + $ns + '\b.*?</' + $ns + '>'
        $structure = [regex]::Replace($structure, $pattern, {
                param($m)
                try {
                    $doc = New-Object System.Xml.XmlDocument
                    $doc.LoadXml($m.Value)
                } catch {
                    throw ("invalid <" + $ns + "> markup: " + $_.Exception.Message)
                }
                "<!-- $ns -->"
            })
    }
    $tagRe = [regex]'(?s)<\s*(/?)\s*([A-Za-z][A-Za-z0-9:-]*)\b(?:[^>"'']|"[^"]*"|''[^'']*'')*(/?)\s*>'
    foreach ($m in $tagRe.Matches($structure)) {
        $closing = $m.Groups[1].Value -eq '/'
        $name = $m.Groups[2].Value.ToLowerInvariant()
        $selfClose = $m.Groups[3].Value -eq '/'
        if ($name -in $void -or $selfClose) { continue }
        if ($closing) {
            if ($stack.Count -eq 0) {
                $issues += New-Issue -Id 'html.balance' -Severity 'error' -Path $rel -Message "stray closing </$name>"
            } elseif ($stack.Peek() -ne $name) {
                $issues += New-Issue -Id 'html.balance' -Severity 'error' -Path $rel -Message "closing </$name> but open <$($stack.Peek())>"
                $stack.Pop() | Out-Null
            } else {
                $stack.Pop() | Out-Null
            }
        } else {
            $stack.Push($name)
        }
    }
    foreach ($unclosed in $stack) {
        $issues += New-Issue -Id 'html.balance' -Severity 'error' -Path $rel -Message "unclosed <$unclosed>"
    }

    # Inline handlers and javascript: URLs are fragile on TV Chromium CSP/lint boundaries.
    if ($noComments -match '(?i)\bon(click|keydown|keyup|load|error|mouseover|focus)\s*=') {
        $issues += New-Issue -Id 'html.inline-handler' -Severity 'warn' -Path $rel -Message 'inline event handler attribute (prefer addEventListener)'
    }
    if ($noComments -match '(?i)javascript\s*:') {
        $issues += New-Issue -Id 'html.javascript-url' -Severity 'warn' -Path $rel -Message 'javascript: URL'
    }

    if ($CheckRefs) {
        $baseDir = [System.IO.Path]::GetDirectoryName($HtmlPath)
        foreach ($ref in (Get-HtmlLocalRefs -Text $noComments)) {
            $r = $ref.Trim()
            if (-not $r -or $r.StartsWith('#') -or $r -match '(?i)^(https?:|data:|blob:|mailto:|javascript:)') { continue }
            $r = ($r -split '[?#]')[0]
            if (-not $r) { continue }
            $target = [System.IO.Path]::GetFullPath((Join-Path $baseDir $r))
            if (-not (Test-Path -LiteralPath $target)) {
                $issues += New-Issue -Id 'html.missing-asset' -Severity 'error' -Path $rel -Message "reference '$ref' not found"
            }
        }
    }
    $issues
}

function Test-CssBalance {
    param([Parameter(Mandatory)][string]$CssPath)
    $rel = Get-RelativeRepoPath $CssPath
    $text = Get-TextFileContent -Path $CssPath
    if ($null -eq $text) { @() }
    $noStrings = [regex]::Replace($text, '(?s)/\*.*?\*/', '')
    $depth = 0
    $issues = @()
    foreach ($ch in $noStrings.ToCharArray()) {
        if ($ch -eq '{') { $depth++ }
        elseif ($ch -eq '}') { $depth--; if ($depth -lt 0) { break } }
    }
    if ($depth -ne 0) {
        $issues += New-Issue -Id 'css.balance' -Severity 'error' -Path $rel -Message "unbalanced braces (depth $depth)"
    }
    if ($noStrings -match '(?i){[^{}]*url\(\s*["'']?https?:') {
        $issues += New-Issue -Id 'css.remote-url' -Severity 'warn' -Path $rel -Message 'remote url() in CSS (offline TV app should be self-contained)'
    }
    $issues
}

# ---------------------------------------------------------------------------
# webOS / TV static-compatibility scan
# ---------------------------------------------------------------------------
$script:TvWebUnsafe = @(
    @{ Id = 'tv.top-level-await'; Severity = 'error'; Pattern = '(?m)^\s*await\s+(?!function)' ; Message = 'top-level await: Chromium <89 cannot parse it (module) and it needs ESM' },
    @{ Id = 'tv.regexp-lookbehind'; Severity = 'error'; Pattern = '(?<![\\\-\[\]])(?<!\?\<)(?<!\?<!)[(]\?<[=!]' ; Message = 'RegExp lookbehind: Chromium <62 fails to parse' },
    @{ Id = 'tv.named-capture'; Severity = 'warn'; Pattern = '\(\?<[A-Za-z][A-Za-z0-9_]*>' ; Message = 'named capture groups require Chromium >=64' },
    @{ Id = 'tv.class-private'; Severity = 'error'; Pattern = '(?m)^\s*(static\s+)?#[A-Za-z_$][\w$]*\s*\(|^\s+(static\s+)?#[A-Za-z_$][\w$]*\s*(=|;)' ; Message = 'class private members (#x): Chromium >=74' },
    @{ Id = 'tv.static-blocks'; Severity = 'error'; Pattern = '(?m)^\s*static\s*\{' ; Message = 'JS class static block: Chromium >=94' },
    @{ Id = 'tv.optional-chain-assign'; Severity = 'error'; Pattern = '\?\.[A-Za-z_$][\w$]*\s*(=[^=]|\+=|-=|\|\|=|&&=|\?\?=)' ; Message = 'assignment to optional chain: Chromium >=? unsupported' },
    @{ Id = 'tv.experimental-import-attributes'; Severity = 'warn'; Pattern = '(?m)^\s*import\b[^;]*\bwith\s*\{|(?m)^\s*import\s+.*\bassert\s*\{' ; Message = 'import attributes/assertions: Chromium >=?? (new versions differ)' },
    @{ Id = 'tv.fetch-priority'; Severity = 'warn'; Pattern = '\{[\s{\[,(][^{};]*\bpriority\s*:\s*[''"](high|low|auto)[''"]' ; Message = 'fetch() priority hint: Chromium >=102 (harmless ignored property)' },
    @{ Id = 'tv.notification-trigger'; Severity = 'warn'; Pattern = '\bNotification\s*\.\s*prototype\s*\.\s*showTrigger' ; Message = 'notification trigger: not on TV Chromium' }
)

function Test-TvStaticCompat {
    <# Flags browser features newer than the target webOS Chromium floor (manifest targetChromium).
       Dot-entries (local browser profiles, preview scripts such as .metrics.mjs) are not packaged
       TV runtime sources and are skipped. #>
    param(
        [Parameter(Mandatory)][string]$AppDir,
        [int]$TargetChromium = 108
    )
    $issues = @()
    if (-not (Test-Path -LiteralPath $AppDir)) { @() }
    # Explicit loop (not Where-Object): a single match would unroll into the FileInfo itself.
    $files = [System.Collections.Generic.List[System.IO.FileInfo]]::new()
    foreach ($candidate in @(Get-ScannedFiles -Root $AppDir -Include '*.js', '*.mjs', '*.html', '*.css')) {
        $crel = Get-RelativeRepoPath $candidate.FullName
        if (-not [System.IO.Path]::GetFileName($crel).StartsWith('.') -and $crel -notmatch '(?i)(^|/)\.') { $files.Add($candidate) }
    }
    foreach ($f in $files) {
        $rel = Get-RelativeRepoPath $f.FullName
        $text = Get-TextFileContent -Path $f.FullName
        if ($null -eq $text) { continue }
        $isHtml = $f.Extension -match '(?i)\.(html?)$'
        $scriptText = $text
        if ($isHtml) {
            # Only inline scripts matter for JS-runtime features.
            $inline = [regex]::Matches($text, '(?is)<script\b[^>]*>(.*?)</script>') | ForEach-Object { $_.Groups[1].Value }
            $scriptText = ($inline -join "`n")
            if ($text -match '(?i)<script\b[^>]*type\s*=\s*["'']module["'']' -and $TargetChromium -lt 80) {
                $issues += New-Issue -Id 'tv.esm' -Severity 'warn' -Path $rel -Message 'type=module requires Chromium >=80'
            }
            foreach ($m in [regex]::Matches($text, '(?i)src\s*=\s*["'']([^"'']+)["'']')) {
                $src = $m.Groups[1].Value
                if ($src -match '(?i)^https?:') { $issues += New-Issue -Id 'tv.remote-script' -Severity 'error' -Path $rel -Message "remote script '$src' (TV app must be offline/self-contained)" }
            }
            if ($text -match '(?i)<link\b[^>]*rel\s*=\s*["''](preload|modulepreload)["'']') {
                $issues += New-Issue -Id 'tv.modulepreload' -Severity 'warn' -Path $rel -Message 'modulepreload: Chromium >=66; not needed for plain classic scripts'
            }
        }
        foreach ($rule in $script:TvWebUnsafe) {
            if ($scriptText -match $rule.Pattern) {
                $m = [regex]::Match($scriptText, $rule.Pattern)
                $ctx = ($m.Value -replace '\s+', ' ')
                # TLA heuristic: only when the file is a module — checked from the surrounding import/export presence.
                $issues += New-Issue -Id $rule.Id -Severity $rule.Severity -Path $rel -Message ($rule.Message + " [near: " + $ctx + "]")
            }
        }
        # CSS shorthands/features not in older Chromium.
        if ($f.Extension -match '(?i)\.css$' -and $TargetChromium -lt 100) {
            if ($text -match '(?m)^\s*[a-z-]+\s*:\s*[^;]*\baspect-ratio\b') { $issues += New-Issue -Id 'tv.css' -Severity 'warn' -Path $rel -Message 'aspect-ratio: Chromium >=88' }
            if ($text -match '(?i)\bcontainer-type\s*:|\b@container\b') { $issues += New-Issue -Id 'tv.container-query' -Severity 'error' -Path $rel -Message 'container queries need Chromium >=105/111 depending on feature' }
            if ($text -match '(?i)\bsubgrid\b') { $issues += New-Issue -Id 'tv.subgrid' -Severity 'error' -Path $rel -Message 'CSS subgrid needs Chromium >=117' }
            if ($text -match '(?i)(?<![\w-])\bview-transition-name\b') { $issues += New-Issue -Id 'tv.view-transition' -Severity 'warn' -Path $rel -Message 'view transitions: Chromium >=111' }
            if ($text -match '(?i):has\s*\(') { $issues += New-Issue -Id 'tv.css-has' -Severity 'warn' -Path $rel -Message ':has(): Chromium >=105' }
            if ($text -match '(?i)color-mix\s*\(') { $issues += New-Issue -Id 'tv.css-color-mix' -Severity 'warn' -Path $rel -Message 'color-mix(): Chromium >=111' }
        }
    }
    $issues
}

# ---------------------------------------------------------------------------
# 1080p TV-safe layout heuristics
# ---------------------------------------------------------------------------
function Test-TvLayoutSafety {
    <# Flags layout constructs that commonly break/lose focus on a 1080p TV (mouse/keyboard-first). #>
    param([Parameter(Mandatory)][string]$AppDir)
    $issues = @()
    if (-not (Test-Path -LiteralPath $AppDir)) { @() }
    $files = Get-ScannedFiles -Root $AppDir -Include '*.html', '*.css'
    foreach ($f in $files) {
        $rel = Get-RelativeRepoPath $f.FullName
        $text = Get-TextFileContent -Path $f.FullName
        if ($null -eq $text) { continue }
        if ($f.Extension -match '(?i)\.(html?)$' -and $text -notmatch '(?i)<meta\s+name\s*=\s*["'']viewport["'']') {
            $issues += New-Issue -Id 'tv.viewport' -Severity 'warn' -Path $rel -Message 'missing viewport meta: webOS app viewport may letterbox/scale'
        }
        # Small interactive targets are hard with a mouse on a TV at 3 m.
        if ($f.Extension -match '(?i)\.css$') {
            if ($text -match '(?i)font-size\s*:\s*(0?\.[0-7]\d*)?(px|rem|em)\b') {
                $text -split "`r?`n" | Where-Object { $_ -match '(?i)font-size\s*:\s*((\d+(\.\d+)?px)|(\.[0-7]\d*px)|(\.[0-7]\d*rem))' } | Select-Object -First 3 | ForEach-Object {
                    $issues += New-Issue -Id 'tv.tiny-font' -Severity 'warn' -Path $rel -Message ("tiny font size under 8px/0.8rem: " + $_.Trim())
                }
            }
        }
        if ($text -match '(?i)position\s*:\s*fixed' -and $f.Extension -match '(?i)\.css$') {
            # allowed (dock/status pins); informational only, not an issue.
        }
    }
    $issues
}

# ---------------------------------------------------------------------------
# Home / Windows-key contract (system HOME, no Orbis)
# ---------------------------------------------------------------------------
function Get-HomeArtifacts {
    <# Locates the Terra-3 client patch and scripts/home cleanup script (read-only). #>
    param()
    $root = Get-RepoRoot
    $homeDir = Join-Path $root 'scripts\home'
    $result = [pscustomobject]@{
        HomeDir  = $homeDir
        ClientPy = $null
        HomeSh   = $null
        PatchDoc = @()
    }
    if (Test-Path -LiteralPath $homeDir) {
        $result.PatchDoc = @(Get-ChildItem -LiteralPath $homeDir -Recurse -File -Force | Where-Object { $_.Extension -match '(?i)^\.(patch|diff|md|txt|json)$' })
        $pyCandidates = @(Get-ChildItem -LiteralPath $homeDir -Recurse -File -Filter '*.py' -ErrorAction SilentlyContinue)
        $pyCandidates = @($pyCandidates | Sort-Object { if ($_.Name -match '(?i)client') { 0 } elseif ($_.Name -match '(?i)home|patch') { 1 } else { 2 } })
        if ($pyCandidates.Count -gt 0) { $result.ClientPy = $pyCandidates[0].FullName }
        $shCandidates = @(Get-ChildItem -LiteralPath $homeDir -Recurse -File -Filter '*.sh' -ErrorAction SilentlyContinue)
        if ($shCandidates.Count -gt 0) { $result.HomeSh = $shCandidates[0].FullName }
    }
    return $result
}

$script:HomeTextContract = @(
    @{ Id = 'home.system-home-launch'; Severity = 'info'; Pattern = 'com\.webos\.app\.home' ; Message = 'launches the webOS system home app' },
    @{ Id = 'home.delay-setting'; Severity = 'warn'; Pattern = '(?i)(override[_ -]?delay|switch[_ -]?delay|home[_ -]?delay|delay[_ -]?ms)' ; Message = 'home path has a configurable override/delay setting' },
    @{ Id = 'home.meta-key-suppress'; Severity = 'error'; Pattern = '(?i)KEY_LEFTMETA|125|meta' ; Message = 'reference to the Meta/Windows key handling' }
)

function Test-HomeContract {
    <# Text-contract check on the Terra-3 home patch. Returns issues (skips nothing; caller decides if absent). #>
    param([Parameter(Mandatory)][string]$ClientPy)
    $issues = @()
    $rel = Get-RelativeRepoPath $ClientPy
    $text = Get-TextFileContent -Path $ClientPy
    if ($null -eq $text) { return @(New-Issue -Id 'home.read' -Severity 'error' -Path $rel -Message 'unreadable home patch') }
    if ($text -match '(?i)orbis') {
        $issues += New-Issue -Id 'home.orbis' -Severity 'error' -Path $rel -Message 'home patch references Orbis (must target system HOME)'
    }
    foreach ($rule in $script:HomeTextContract) {
        if ($text -notmatch $rule.Pattern) {
            $issues += New-Issue -Id $rule.Id -Severity $rule.Severity -Path $rel -Message $rule.Message
        }
    }
    $legacy = ($text -match '/tmp/netinput\.pointer\.sock') -and
        ($text -match '(?i)type:\s*button|name:\s*HOME|"HOME"|''HOME''')
    $launcher = ($text -match '(?i)\bluna-send\b') -and ($text -match 'com\.webos\.app\.home')
    if (-not ($legacy -or $launcher)) {
        $issues += New-Issue -Id 'home.action' -Severity 'error' -Path $rel -Message 'no system HOME action (luna-send launch or legacy framed HOME)'
    }
    $issues
}

# ---------------------------------------------------------------------------
# Dock integration contract
# ---------------------------------------------------------------------------
function Get-DockArtifacts {
    param()
    $dock = Join-Path (Get-RepoRoot) 'dock'
    $art = [pscustomobject]@{ DockDir = $dock; Exists = (Test-Path -LiteralPath $dock); Docs = @(); Appinfo = $null; Html = @(); Scripts = @() }
    if (-not $art.Exists) { return $art }
    $files = @(Get-ChildItem -LiteralPath $dock -Recurse -File -Force -ErrorAction SilentlyContinue | Where-Object { $_.FullName -notmatch '(?i)[\\/](node_modules|\.git)[\\/]' })
    $art.Docs = @($files | Where-Object { $_.Name -match '(?i)contract|integration|readme' -and $_.Extension -match '(?i)\.(md|txt|json)$' })
    $ai = $files | Where-Object { $_.FullName -match '(?i)[\\/]appinfo\.json$' } | Select-Object -First 1
    if ($ai) { $art.Appinfo = $ai.FullName }
    $art.Html = @($files | Where-Object { $_.Extension -match '(?i)\.html?$' } | ForEach-Object { $_.FullName })
    $art.Scripts = @($files | Where-Object { $_.Extension -match '(?i)^\.(js|mjs|ts)$' } | ForEach-Object { $_.FullName })
    return $art
}

# ---------------------------------------------------------------------------
# Synergy hot-corner config tooling (Terra 1)
# ---------------------------------------------------------------------------
$script:CornerTargetPx = 2

function Get-CornerToolingFiles {
    param()
    $root = Join-Path (Get-RepoRoot) 'tools\synergy'
    if (-not (Test-Path -LiteralPath $root)) { @() }
    return @(Get-ChildItem -LiteralPath $root -Recurse -File -Force -ErrorAction SilentlyContinue |
        Where-Object {
            $_.FullName -notmatch '(?i)[\\/](node_modules|\.git)[\\/]' -and
            ($_.Extension -match '(?i)^\.(ps1|py|js|mjs|sh|conf|json|md|txt|ya?ml)$')
        } | Sort-Object FullName)
}

$script:CornerConfigContract = @(
    @{ Id = 'corner.link-format'; Severity = 'error'; Pattern = '(?im)^[ \t]+(up|down|left|right)[ \t]*\([ \t]*\d+(\.\d+)?[ \t]*,[ \t]*\d+(\.\d+)?[ \t]*\)[ \t]*=[ \t]*[\w.\-]+[ \t]*\([ \t]*\d+(\.\d+)?[ \t]*,[ \t]*\d+(\.\d+)?[ \t]*\)' ; Message = 'Synergy link line (dir(percent,pixel) = screen(x,y))' },
    @{ Id = 'corner.switch-delay'; Severity = 'warn'; Pattern = '(?i)switchDelay' ; Message = 'missing a configurable switchDelay option' },
    @{ Id = 'corner.corners-option'; Severity = 'info'; Pattern = '(?i)switchC?orners?' ; Message = 'corner option (switchCorners) considered' },
    @{ Id = 'corner.override'; Severity = 'warn'; Pattern = '(?i)override' ; Message = 'delay override setting' }
)

function Test-CornerTooling {
    <# Text contract for the 2px top-left corner + delay/override tooling.
       Do not name the parameter $Files: that shadows PowerShell's automatic $Files variable. #>
    param([string[]]$Paths)
    if (-not $Paths) { $Paths = Get-CornerToolingFiles }
    $issues = @()
    if ($Paths.Count -eq 0) { @() }
    $corpus = ''
    foreach ($f in $Paths) {
        $t = Get-TextFileContent -Path $f
        if ($null -eq $t) { continue }
        $corpus += "`n# file: $f`n" + $t
    }
    foreach ($rule in $script:CornerConfigContract) {
        if ($corpus -notmatch $rule.Pattern) {
            $issues += New-Issue -Id $rule.Id -Severity $rule.Severity -Path 'tools/synergy' -Message $rule.Message
        }
    }
    $issues
}

function Test-CornerGeometry {
    <# Pure-math helper: verifies the percentage for a 2px edge on a given screen size.
       Used by tests/unit and the build report. #>
    param([int]$ScreenWidth = 3840, [int]$ScreenHeight = 2160, [int]$CornerPx = $script:CornerTargetPx)
    $w = [math]::Round(($CornerPx / [double]$ScreenWidth) * 100.0, 3)
    $h = [math]::Round(($CornerPx / [double]$ScreenHeight) * 100.0, 3)
    return [pscustomobject]@{
        Screen = "$($ScreenWidth)x$($ScreenHeight)"
        PctWidth = $w
        PctHeight = $h
        PxAtWidth = [math]::Round(($w / 100.0) * $ScreenWidth, 2)
        PxAtHeight = [math]::Round(($h / 100.0) * $ScreenHeight, 2)
    }
}
