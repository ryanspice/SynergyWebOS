#requires -Version 7.0
<#
.SYNOPSIS
    Checks relative Markdown links and image paths in tracked Markdown files.

.DESCRIPTION
    Collects every link/image target of the form ](<target>) from tracked
    *.md files and verifies that relative targets resolve to an existing file
    (or directory) in the working tree. External http(s) links, mailto links
    and in-page anchors are not fetched.

    The check is intentionally conservative: a relative target that does not
    exist is reported. Links may point at existing files or (for workstream
    folders) existing directories.

    Files are taken from git (`ls-files`, or `ls-tree` for -GitWorkTree) and
    fall back to a filesystem scan when the tree is not a git checkout.

    Exit code 0 = all required relative links resolve, 1 = at least one broken
    link.

.PARAMETER Path
    Checker root. Defaults to the parent of this script's folder.
.PARAMETER AllowMissing
    Comma-separated repository-relative prefixes (e.g. 'app', 'tools') whose
    targets may be missing but are only warned about, for folders owned by
    workstreams that have not landed yet. Pass as a comma-separated string so
    it works through Windows PowerShell 5.1 and 7:
      -AllowMissing 'app,tools,scripts'
    Defaults to empty for a complete repository.
.PARAMETER GitWorkTree
    Optional git checkout to list Markdown files from. Paths are read from that
    commit (default HEAD), so publishing a staged subset can be checked from
    the working tree. Targets resolve against -Path.
#>
[CmdletBinding()]
param(
    # Path and GitWorkTree are scalar. AllowMissing is a comma-separated
    # string rather than [string[]]: with a default plus a trailing array
    # parameter, a positional array can bind to the wrong parameter, and
    # Windows PowerShell 5.1 cannot convert an array to a [string] parameter
    # at all. A comma string is reliable through `pwsh -File` and in 5.1.
    [string]$Path = (Split-Path -Parent $PSScriptRoot),
    [string]$GitWorkTree = '',
    [string]$AllowMissing = ''
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$AllowMissingList = @(
    if (-not [string]::IsNullOrWhiteSpace($AllowMissing)) {
        $AllowMissing -split ',' | ForEach-Object { $_.Trim().Replace('\', '/').Trim('/') } | Where-Object { $_ } | ForEach-Object { "$_/" }
    }
)

$root = (Resolve-Path -LiteralPath $Path).Path
Push-Location -LiteralPath $root
try {
    $files = @()
    $gitRoot = if ($GitWorkTree) { (Resolve-Path -LiteralPath $GitWorkTree).Path } else { $root }
    $gitList = @()
    & git -C $gitRoot @('rev-parse', '--git-dir') 2>$null | Out-Null
    if ($LASTEXITCODE -eq 0) {
        if ($GitWorkTree) {
            # ls-tree does not reliably apply pathspecs, so filter after listing.
            $gitList = @(& git -C $gitRoot @('ls-tree', '-r', '--name-only', 'HEAD') 2>$null |
                    Where-Object { $_ -like '*.md' })
        }
        else {
            $gitList = @(& git @('ls-files', '*.md') 2>$null)
        }
    }
    if ($gitList) {
        # Paths are relative to $gitRoot; the checker root is $root.
        $files = @($gitList | Where-Object { $_ } | ForEach-Object {
                $abs = [System.IO.Path]::GetFullPath([System.IO.Path]::Combine($gitRoot, $_))
                [System.IO.Path]::GetRelativePath($root, $abs).Replace('\', '/')
            })
    }
    if (-not $files -or $files.Count -eq 0) {
        $files = @(Get-ChildItem -Recurse -File -Filter *.md | Where-Object { $_.FullName -notmatch '[\\/]node_modules[\\/]' } | ForEach-Object {
                [System.IO.Path]::GetRelativePath($root, $_.FullName).Replace('\', '/')
            })
    }

    $broken = [System.Collections.Generic.List[string]]::new()
    $warned = [System.Collections.Generic.List[string]]::new()
    $linkCount = 0

    foreach ($file in $files) {
        $full = Join-Path $root $file
        if (-not (Test-Path -LiteralPath $full)) { continue }
        $lines = Get-Content -LiteralPath $full
        $inFence = $false
        for ($i = 0; $i -lt $lines.Count; $i++) {
            $line = $lines[$i]
            if ($line -match '^\s*(```|~~~)') { $inFence = -not $inFence; continue }
            if ($inFence) { continue }

            # Strip inline code spans so `docs/x.md` examples do not count.
            $stripped = [regex]::Replace($line, '`[^`]*`', '')

            foreach ($m in [regex]::Matches($stripped, '\]\(([^)\s]+)(?:\s+"[^"]*")?\)')) {
                $target = $m.Groups[1].Value
                if ($target -match '^[a-zA-Z][a-zA-Z0-9+.-]*:') { continue }   # http:, https:, mailto:, ...
                if ($target.StartsWith('#')) { continue }                        # in-page anchor
                $linkCount++
                $clean = ($target -split '#')[0]
                $clean = [System.Uri]::UnescapeDataString($clean)
                if ([string]::IsNullOrWhiteSpace($clean)) { continue }
                $clean = $clean.Replace('/', [System.IO.Path]::DirectorySeparatorChar)

                $candidate = if ($clean.StartsWith([System.IO.Path]::DirectorySeparatorChar)) {
                    Join-Path $root $clean.TrimStart([System.IO.Path]::DirectorySeparatorChar)
                }
                else {
                    Join-Path (Split-Path -Parent $full) $clean
                }
                # Filesystem APIs throw on paths whose last segment is > 255
                # chars; such a candidate is simply reported as missing.
                $exists = $false
                try { $exists = Test-Path -LiteralPath $candidate } catch { $exists = $false }
                if (-not $exists) {
                    $relTarget = [System.IO.Path]::GetRelativePath($root, [System.IO.Path]::GetFullPath($candidate)).Replace('\', '/').TrimStart('/')
                    $allowed = @($AllowMissingList | Where-Object { $_ -and $relTarget.StartsWith($_, [System.StringComparison]::Ordinal) }).Count -gt 0
                    if ($allowed) { $warned.Add("${file}:$($i + 1)  ->  $target  (pending: $relTarget)") }
                    else { $broken.Add("${file}:$($i + 1)  ->  $target") }
                }
            }
        }
    }

    Write-Host "check-docs-links: $(@($files).Count) Markdown file(s), $linkCount relative link(s); $($broken.Count) broken, $($warned.Count) pending."
    foreach ($w in $warned) { Write-Host "  $w" -ForegroundColor Yellow }
    if ($broken.Count -gt 0) {
        $broken | ForEach-Object { Write-Host "  $_" }
        exit 1
    }
    Write-Host 'check-docs-links: PASS' -ForegroundColor Green
    exit 0
}
finally {
    Pop-Location
}
