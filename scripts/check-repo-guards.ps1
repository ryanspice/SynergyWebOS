#requires -Version 7.0
<#
.SYNOPSIS
    Fails when tracked or candidate files violate the repository hygiene rules.

.DESCRIPTION
    Repository-owned guard used by CI (.github/workflows/ci.yml) and runnable
    locally before committing. It checks file NAMES and small TEXT contents of:

      - staged files when -Staged is given (pre-commit use), otherwise
      - files tracked by the current commit (CI / post-commit use).

    Forbidden names/classes: secrets, env files, private keys, certificates,
    device credentials, Synergy/Symless bundles, backup/working copies, build
    output, dependency folders and log/state files.

    Content scan (text files only, first 256 KiB per file) looks for:
      - private key blocks
      - common token formats (GitHub, AWS, Slack, Google API keys)
      - Synergy 3 license/activation markers and machine identifiers

    Exit code 0 = clean, 1 = violations found. No files are modified.
    This script is a safety net, not a guarantee.

.PARAMETER Staged
    Inspect the staged index (git diff --cached --name-only) instead of HEAD.

.PARAMETER MaxBytes
    Maximum bytes of each text file to read for the content scan.
#>
[CmdletBinding()]
param(
    [switch]$Staged,
    [int]$MaxBytes = 262144
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

function Get-RepoFileList {
    param([switch]$StagedIndex)
    if ($StagedIndex) {
        # Works before the first commit as well (compares the index to HEAD).
        $out = & git @('diff', '--cached', '--name-only', '--diff-filter=ACMR') 2>$null
    }
    else {
        $out = & git @('ls-files', '--cached') 2>$null
    }
    return @($out | Where-Object { $_ -and $_.Trim() -ne '' } | ForEach-Object { $_.Trim() })
}

function Get-FileBlob {
    param([string]$Path, [switch]$StagedIndex)
    if ($StagedIndex) {
        # Read the index blob as raw bytes via the git object id.
        $blobId = (& git @('rev-parse', ":$Path") 2>$null | Select-Object -First 1)
        if ($LASTEXITCODE -ne 0 -or [string]::IsNullOrWhiteSpace($blobId)) { return $null }
        $psi = [System.Diagnostics.ProcessStartInfo]::new('git')
        $psi.ArgumentList.Add('cat-file')
        $psi.ArgumentList.Add('blob')
        $psi.ArgumentList.Add($blobId.Trim())
        $psi.RedirectStandardOutput = $true
        $psi.UseShellExecute = $false
        $process = [System.Diagnostics.Process]::Start($psi)
        $memory = New-Object System.IO.MemoryStream
        $process.StandardOutput.BaseStream.CopyTo($memory)
        $process.WaitForExit()
        if ($process.ExitCode -ne 0) { return $null }
        return $memory.ToArray()
    }
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) { return $null }
    return [System.IO.File]::ReadAllBytes((Resolve-Path -LiteralPath $Path).Path)
}

$nameRules = @(
    @{ Pattern = '(?i)(^|/)(\.env|\.env\..*)$'; Reason = 'environment/secret file' },
    @{ Pattern = '(?i)\.(pem|key|p12|pfx|jks|keystore|ppk)$'; Reason = 'private key or certificate store' },
    @{ Pattern = '(?i)(^|/)id_(rsa|dsa|ecdsa|ed25519)(\.pub)?$'; Reason = 'SSH key material' },
    @{ Pattern = '(?i)(^|/)(license|licence)(\.[a-z0-9]+)?$'; Reason = 'license key file' },
    @{ Pattern = '(?i)(\.lic|\.license)$'; Reason = 'license key file' },
    @{ Pattern = '(?i)\.(bak|orig|rej|old|save|sav)$|~$'; Reason = 'backup/working copy' },
    @{ Pattern = '(?i)(^|/)backups/'; Reason = 'backup directory' },
    @{ Pattern = '(?i)(^|/)synergy\.conf$|\.synergy'; Reason = 'Synergy config snapshot' },
    @{ Pattern = '(?i)(^|/)(db|local)\.json$'; Reason = 'Synergy database snapshot' },
    @{ Pattern = '(?i)\.asar$'; Reason = 'proprietary Electron bundle' },
    @{ Pattern = '(?i)^(Synergy/|synergy-core)'; Reason = 'proprietary Synergy binary/bundle' },
    @{ Pattern = '(?i)(^|/)node_modules/'; Reason = 'dependency folder' },
    @{ Pattern = '(?i)\.ipk$'; Reason = 'built package (releases live in dist/release or releases/)' },
    @{ Pattern = '(?i)(^|/)(dist|build|out|coverage|\.svelte-kit|\.vite)/'; Reason = 'build output' },
    @{ Pattern = '(?i)\.(log|pid)$|(^|/)logs/'; Reason = 'log/state file' },
    @{ Pattern = '(?i)(^|/)\.webos/'; Reason = 'webOS CLI device credentials' },
    @{ Pattern = '(?i)(^|/)(secrets|credentials)/'; Reason = 'secret storage' },
    @{ Pattern = '(?i)\.htpasswd$'; Reason = 'credential file' }
)

$contentRules = @(
    @{ Pattern = '-----BEGIN (RSA |EC |OPENSSH |DSA |PGP )?PRIVATE KEY-----'; Reason = 'private key block' },
    @{ Pattern = 'gh[pousr]_[A-Za-z0-9]{36,}'; Reason = 'GitHub token' },
    @{ Pattern = 'github_pat_[A-Za-z0-9_]{20,}'; Reason = 'GitHub fine-grained token' },
    @{ Pattern = 'AKIA[0-9A-Z]{16}'; Reason = 'AWS access key id' },
    @{ Pattern = 'xox[baprs]-[A-Za-z0-9-]{10,}'; Reason = 'Slack token' },
    @{ Pattern = 'AIza[0-9A-Za-z\-_]{35}'; Reason = 'Google API key' }
)

$binaryExtensions = @('.png', '.jpg', '.jpeg', '.gif', '.webp', '.ico', '.ipk', '.zip', '.wgt', '.woff', '.woff2', '.ttf', '.otf', '.mp4', '.webm')

$violations = [System.Collections.Generic.List[string]]::new()
$files = Get-RepoFileList -StagedIndex:$Staged
$checked = 0

foreach ($file in $files) {
    # The root LICENSE file is the project license, not a license key.
    if ($file -eq 'LICENSE') { continue }
    foreach ($rule in $nameRules) {
        if ($file -match $rule.Pattern) { $violations.Add("NAME  $file  -> $($rule.Reason)") ; break }
    }

    $ext = [System.IO.Path]::GetExtension($file).ToLowerInvariant()
    if ($binaryExtensions -contains $ext) { continue }

    $bytes = Get-FileBlob -Path $file -StagedIndex:$Staged
    if ($null -eq $bytes -or $bytes.Length -eq 0) { continue }
    $slice = if ($bytes.Length -gt $MaxBytes) { $bytes[0..($MaxBytes - 1)] } else { $bytes }
    # Skip obvious binary content (NUL byte in the first KiB).
    if ($slice[0..([Math]::Min(1023, $slice.Length - 1))] -contains 0) { continue }
    $text = [System.Text.Encoding]::UTF8.GetString($slice)
    $checked++
    foreach ($rule in $contentRules) {
        if ($text -match $rule.Pattern) { $violations.Add("CONTENT  $file  -> $($rule.Reason)") }
    }
}

$count = $violations.Count
Write-Host "repo-guards: scanned $(@($files).Count) path(s), $checked text file(s); $count violation(s)."
if ($count -gt 0) {
    $violations | ForEach-Object { Write-Host "  $_" }
    Write-Host 'Remove the files from the change; do not commit secrets, backups or proprietary bundles.' -ForegroundColor Red
    exit 1
}
Write-Host 'repo-guards: PASS' -ForegroundColor Green
exit 0
