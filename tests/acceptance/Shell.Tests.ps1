# POSIX shell checks for the TV control script (Web-high's app/client/tvctl.sh, or the
# home patch's shell). Uses a local POSIX sh (Git for Windows) or WSL bash if available;
# SKIPs with the missing prerequisite otherwise. Never runs on the TV.
$map = [ordered]@{}
$posixSh = Get-ShPath
$wsl = @(Get-Command wsl -ErrorAction SilentlyContinue)[0]

function Get-ShellTargets {
    $root = Get-RepoRoot
    $targets = @()
    $appClient = Join-Path $root 'app\client'
    if (Test-Path -LiteralPath $appClient) { $targets += @(Get-ChildItem -LiteralPath $appClient -File -Filter '*.sh') }
    $homeDir = Join-Path $root 'scripts\home'
    if (Test-Path -LiteralPath $homeDir) { $targets += @(Get-ChildItem -LiteralPath $homeDir -File -Filter '*.sh') }
    return @($targets | Where-Object { $_.FullName -notmatch '(?i)[\\/](node_modules|\.git)[\\/]' })
}

function Invoke-ShSyntaxCheck {
    <# Returns a description string on success, or $null when no POSIX shell can run here.
       A shell that cannot even start (sandbox pipe restriction) is "unavailable", not a
       script failure; a real parse error always throws. #>
    param([string[]]$Paths)
    $shells = @()
    if ($posixSh) { $shells += [pscustomobject]@{ Exe = $posixSh; Args = { param($p) @('-n', ($p -replace '\\', '/')) }; Kind = 'sh -n' } }
    if ($wsl -and $wsl.Source) { $shells += [pscustomobject]@{ Exe = $wsl.Source; Args = { param($p) @('bash', '-n', (Get-WslPath $p)) }; Kind = 'wsl bash -n' } }
    foreach ($shell in $shells) {
        $brokenEnv = $false
        foreach ($p in $Paths) {
            $r = Test-ExitWithCode -FilePath $shell.Exe -Arguments (& $shell.Args $p)
            # WSL interop emits UTF-16 error text; normalise before matching.
            $out = (($r.Output -replace "`0", '') -replace '\s+', ' ')
            $broken = $out -match '(?i)couldn.t create signal pipe|fatal error|E_ACCESSDENIED|Access is denied'
            if ($broken) {
                # Confirm with a trivial script: a shell that cannot start is "unavailable", not a
                # script failure; a real parse error always fails.
                $probe = Join-Path ([System.IO.Path]::GetTempPath()) ('shprobe-' + [guid]::NewGuid().ToString('N') + '.sh')
                [System.IO.File]::WriteAllText($probe, "echo ok`n")
                try {
                    $probeArgs = if ($shell.Kind -like 'wsl*') { @('bash', '-n', (Get-WslPath $probe)) } else { @('-n', $probe) }
                    $pr = Test-ExitWithCode -FilePath $shell.Exe -Arguments $probeArgs
                    $pout = (($pr.Output -replace "`0", '') -replace '\s+', ' ')
                    $broken = ($pr.ExitCode -ne 0) -and ($pout -match '(?i)couldn.t create signal pipe|fatal error|E_ACCESSDENIED|Access is denied')
                } finally { Remove-Item -LiteralPath $probe -Force -ErrorAction SilentlyContinue }
                if ($broken) { $brokenEnv = $true; break }
            }
            if ($r.ExitCode -ne 0) { throw ("$($shell.Kind) failed for " + (Get-RelativeRepoPath $p) + ": " + $out.Trim()) }
        }
        if (-not $brokenEnv) { return "ok ($($shell.Kind))" }
    }
    return $null
}

$map['shell: tvctl.sh (and any scripts/home shell) parse under POSIX sh'] = {
    $targets = @(Get-ShellTargets)
    if ($targets.Count -eq 0) { Skip-Test 'no shell scripts in app/client or scripts/home yet' }
    $result = Invoke-ShSyntaxCheck -Paths @($targets | ForEach-Object { $_.FullName })
    if ($null -eq $result) { Skip-Test 'no local POSIX shell (install Git for Windows sh or WSL); shellcheck unavailable' }
    "$($targets.Count) script(s): $result"
}
$map['shell: tvctl.sh is LF-only, has a shebang and avoids bashisms'] = {
    $tvctl = Join-Path (Get-RepoRoot) 'app\client\tvctl.sh'
    if (-not (Test-Path -LiteralPath $tvctl)) { Block-Test 'app/client/tvctl.sh missing' }
    $bytes = [System.IO.File]::ReadAllBytes($tvctl)
    Assert-True ($bytes[0] -eq 0x23 -and $bytes[1] -eq 0x21) 'shebang first'
    Assert-True (-not ($bytes | Where-Object { $_ -eq 13 })) 'no CR bytes (CRLF breaks busybox sh)'
    $text = [System.Text.Encoding]::UTF8.GetString($bytes)
    Assert-NotMatch $text '(?i)\[\[|function \w+\(|<<<|\bsource\b' 'no bashisms ([[, function(), here-strings, source)'
    'lf-only posix sh'
}
$map['shell: tvctl.sh does not hardcode a legacy launcher and defaults to system HOME'] = {
    $tvctl = Join-Path (Get-RepoRoot) 'app\client\tvctl.sh'
    if (-not (Test-Path -LiteralPath $tvctl)) { Block-Test 'app/client/tvctl.sh missing' }
    $text = Get-TextFileContent -Path $tvctl
    Assert-NotMatch $text '(?i)com\.canopy' 'no legacy launcher id'
    Assert-Match $text '(?i)HOME_TARGET|home[-_]target' 'home target is a parameter/config'
    Assert-Match $text '(?i)system' 'system HOME target supported'
    'home defaults ok'
}

$map
