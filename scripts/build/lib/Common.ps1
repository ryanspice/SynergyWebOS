# Shared helpers for the synergy-tv build/QA pipeline.
# Dot-source from scripts/build/*.ps1:  . (Join-Path $PSScriptRoot 'lib\Common.ps1')
# Owned by Flash-off 2 (scripts/build/**, tests/**). Keep this dependency-free
# (PowerShell 5.1+ and 7.x) so a fresh checkout builds without installation.

Set-StrictMode -Version 2.0
$ErrorActionPreference = 'Stop'

# Repo root is two levels above scripts/build/lib.
$script:RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..\..')).Path
$script:BuildDir = Join-Path $RepoRoot 'scripts\build'
$script:DistDir = Join-Path $RepoRoot 'dist'

# .tools/ares-cli lives beside the repo (B:\Dev\webos\.tools\ares-cli) but never
# inside it. Override with -AresCliDir / $env:ARES_CLI_DIR.
$script:DefaultAresCandidates = @(
    (Join-Path (Split-Path $RepoRoot -Parent) '.tools\ares-cli'),
    (Join-Path $env:USERPROFILE '.webos\ares-cli'),
    (Join-Path $env:USERPROFILE '.ares-cli')
)

function Get-RepoRoot { return $script:RepoRoot }
function Get-DistDir { return $script:DistDir }

function Resolve-RelPath {
    param([Parameter(Mandatory)][object]$Path)
    $p = if ($Path -is [System.IO.FileSystemInfo]) { $Path.FullName } else { [string]$Path }
    if ([System.IO.Path]::IsPathRooted($p)) { return [System.IO.Path]::GetFullPath($p) }
    return [System.IO.Path]::GetFullPath((Join-Path $script:RepoRoot $p))
}

function Get-RelativeRepoPath {
    <# Accepts a path string or FileInfo/DirectoryInfo (avoids the [string] parameter
       binding failure when a FileInfo is passed). #>
    param([Parameter(Mandatory)][object]$Path)
    $p = if ($Path -is [System.IO.FileSystemInfo]) { $Path.FullName } else { [string]$Path }
    $full = [System.IO.Path]::GetFullPath($p)
    $root = $script:RepoRoot.TrimEnd('\', '/') + [System.IO.Path]::DirectorySeparatorChar
    if (-not $full.StartsWith($root, [System.StringComparison]::OrdinalIgnoreCase)) { return $full }
    return $full.Substring($root.Length).Replace('\', '/')
}

function Find-AresCli {
    <# Returns the ares-package.cmd path and the CLI root, or $null when absent. #>
    param([string]$AresCliDir)
    $candidates = @()
    if ($AresCliDir) { $candidates += $AresCliDir }
    if ($env:ARES_CLI_DIR) { $candidates += $env:ARES_CLI_DIR }
    $candidates += $script:DefaultAresCandidates
    $global:LASTEXITCODE = 0
    foreach ($dir in $candidates) {
        if (-not $dir) { continue }
        foreach ($exe in @('ares-package.cmd', 'ares-package.ps1')) {
            $pkg = Join-Path $dir "node_modules\.bin\$exe"
            if (Test-Path -LiteralPath $pkg) {
                return [pscustomobject]@{ PackageCmd = $pkg; CliDir = $dir }
            }
        }
    }
    return $null
}

function Invoke-AresPackage {
    <# Runs ares-package with flattened output. Returns exit code + captured text. #>
    param(
        [Parameter(Mandatory)][string]$AresPackageCmd,
        [Parameter(Mandatory)][string[]]$Arguments,
        [int]$TimeoutSeconds = 180
    )
    $psi = New-Object System.Diagnostics.ProcessStartInfo
    $psi.FileName = $env:ComSpec
    $quotedArgs = ($Arguments | ForEach-Object { '"' + ($_ -replace '"', '""') + '"' }) -join ' '
    $psi.Arguments = '/d /c ""' + $AresPackageCmd + '" ' + $quotedArgs + '"'
    $psi.RedirectStandardOutput = $true
    $psi.RedirectStandardError = $true
    $psi.UseShellExecute = $false
    $psi.CreateNoWindow = $true
    $psi.WorkingDirectory = $script:RepoRoot
    $proc = [System.Diagnostics.Process]::Start($psi)
    $out = $proc.StandardOutput.ReadToEndAsync()
    $err = $proc.StandardError.ReadToEndAsync()
    if (-not $proc.WaitForExit($TimeoutSeconds * 1000)) {
        try { $proc.Kill() } catch { }
        return [pscustomobject]@{ ExitCode = -1; StdOut = ''; StdErr = "timed out after ${TimeoutSeconds}s" }
    }
    $proc.WaitForExit()
    return [pscustomobject]@{
        ExitCode = $proc.ExitCode
        StdOut   = $out.Result
        StdErr   = $err.Result
    }
}

function Get-ToolVersion {
    param([Parameter(Mandatory)][string[]]$Command)
    try {
        $psi = New-Object System.Diagnostics.ProcessStartInfo
        $psi.FileName = $Command[0]
        if ($Command.Count -gt 1) { $psi.Arguments = ($Command[1..($Command.Count - 1)] | ForEach-Object { '"' + $_ + '"' }) -join ' ' }
        $psi.RedirectStandardOutput = $true
        $psi.RedirectStandardError = $true
        $psi.UseShellExecute = $false
        $psi.CreateNoWindow = $true
        $p = [System.Diagnostics.Process]::Start($psi)
        $text = $p.StandardOutput.ReadToEnd() + $p.StandardError.ReadToEnd()
        if (-not $p.WaitForExit(20000)) { try { $p.Kill() } catch { }; return $null }
        return (($text -split "`r?`n") | Where-Object { $_.Trim() } | Select-Object -First 1)
    } catch {
        return $null
    }
}

function Get-RunEvidence {
    <# Environment identity for reproducible receipts: tool versions + UTC date. #>
    param([string]$AresPackageCmd)
    $aresVer = $null
    if ($AresPackageCmd) {
        $psi = New-Object System.Diagnostics.ProcessStartInfo
        $psi.FileName = $env:ComSpec
        $psi.Arguments = '/d /c ""' + $AresPackageCmd + '" --version"'
        $psi.RedirectStandardOutput = $true
        $psi.RedirectStandardError = $true
        $psi.UseShellExecute = $false
        $psi.CreateNoWindow = $true
        try {
            $p = [System.Diagnostics.Process]::Start($psi)
            $t = ($p.StandardOutput.ReadToEnd() + $p.StandardError.ReadToEnd())
            if ($p.WaitForExit(20000)) {
                if ($t -match '\d+\.\d+\.\d+') { $aresVer = $Matches[0] } else { $aresVer = $t.Trim() }
            }
        } catch { $aresVer = $null }
    }
    return [ordered]@{
        dateUtc         = (Get-Date).ToUniversalTime().ToString('yyyy-MM-ddTHH:mm:ssZ')
        os              = [System.Environment]::OSVersion.VersionString
        powershell      = $PSVersionTable.PSVersion.ToString()
        node            = (Get-ToolVersion -Command @('node', '--version'))
        python          = (Get-ToolVersion -Command @('python', '--version'))
        aresPackage     = $aresVer
        aresPackagePath = $AresPackageCmd
    }
}

function Invoke-Native {
    <# Runs a native command with a hard timeout. Returns ExitCode/Output/Error/TimedOut. #>
    param(
        [Parameter(Mandatory)][string]$FilePath,
        [string[]]$Arguments = @(),
        [string]$WorkingDirectory = $script:RepoRoot,
        [int]$TimeoutMs = 600000
    )
    $psi = New-Object System.Diagnostics.ProcessStartInfo
    $psi.FileName = $FilePath
    $psi.Arguments = ($Arguments | ForEach-Object { if ($_ -match '[\s"]') { '"' + ($_ -replace '"', '\"') + '"' } else { $_ } }) -join ' '
    $psi.RedirectStandardOutput = $true
    $psi.RedirectStandardError = $true
    $psi.UseShellExecute = $false
    $psi.CreateNoWindow = $true
    $psi.WorkingDirectory = $WorkingDirectory
    if ($psi.EnvironmentVariables) { $psi.EnvironmentVariables['NO_COLOR'] = '1' }
    $p = [System.Diagnostics.Process]::Start($psi)
    $o = $p.StandardOutput.ReadToEndAsync()
    $e = $p.StandardError.ReadToEndAsync()
    if (-not $p.WaitForExit($TimeoutMs)) {
        try { $p.Kill() } catch { }
        return [pscustomobject]@{ ExitCode = -1; Output = ''; Error = "timed out after ${TimeoutMs}ms"; TimedOut = $true }
    }
    $p.WaitForExit()
    return [pscustomobject]@{
        ExitCode = $p.ExitCode
        Output   = $o.Result
        Error    = $e.Result
        TimedOut = $false
    }
}

function Invoke-PowerShellScript {
    <# Runs a .ps1 in a child PowerShell using the CURRENT host executable, so a test can
       assert on the script's own exit code without terminating itself. #>
    param(
        [Parameter(Mandatory)][string]$ScriptPath,
        [string[]]$Arguments = @(),
        [string]$WorkingDirectory,
        [int]$TimeoutMs = 600000
    )
    return Invoke-Native -FilePath (Get-Process -Id $PID).Path `
        -Arguments (@('-NoProfile', '-NonInteractive', '-ExecutionPolicy', 'Bypass', '-File', $ScriptPath) + $Arguments) `
        -WorkingDirectory $(if ($WorkingDirectory) { $WorkingDirectory } else { $script:RepoRoot }) -TimeoutMs $TimeoutMs
}

function Get-HostExe { return (Get-Process -Id $PID).Path }

function Write-Step {
    param([string]$Name, [string]$Message)
    $color = if ($Host.Name -eq 'ConsoleHost') { 'Cyan' } else { $null }
    Write-Host ''
    Write-Host ("== $Name == " + $Message) -ForegroundColor $color
}

function Write-Ok   { param([string]$Message) Write-Host "PASS  $Message" -ForegroundColor Green }
function Write-Warn { param([string]$Message) Write-Host "WARN  $Message" -ForegroundColor Yellow }
function Write-Fail { param([string]$Message) Write-Host "FAIL  $Message" -ForegroundColor Red }
function Write-Skip { param([string]$Message) Write-Host "SKIP  $Message" -ForegroundColor DarkGray }

function Exit-WithCode {
    param([int]$Code, [string]$Message)
    if ($Message) { if ($Code -eq 0) { Write-Ok $Message } else { Write-Fail $Message } }
    exit $Code
}
