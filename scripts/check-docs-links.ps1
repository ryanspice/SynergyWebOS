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
    folders) existing directories. GitHub-style links to not-yet-populated
    workstream folders are therefore accepted only once the directory exists.

    Exit code 0 = all relative links resolve, 1 = at least one broken link.

.PARAMETER Path
    Repository root. Defaults to the parent of this script's folder.
#>
[CmdletBinding()]
param(
    [string]$Path = (Split-Path -Parent $PSScriptRoot)
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$root = (Resolve-Path -LiteralPath $Path).Path
Push-Location -LiteralPath $root
try {
    $files = @(& git @('ls-files', '*.md') 2>$null)
    if (-not $files -or $files.Count -eq 0) {
        $files = @(Get-ChildItem -Recurse -File -Filter *.md | Where-Object { $_.FullName -notmatch '[\\/]node_modules[\\/]' } | ForEach-Object {
                [System.IO.Path]::GetRelativePath($root, $_.FullName).Replace('\', '/')
            })
    }

    $broken = [System.Collections.Generic.List[string]]::new()
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
                if (-not (Test-Path -LiteralPath $candidate)) {
                    $broken.Add("${file}:$($i + 1)  ->  $target")
                }
            }
        }
    }

    Write-Host "check-docs-links: $($files.Count) Markdown file(s), $linkCount relative link(s); $($broken.Count) broken."
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
