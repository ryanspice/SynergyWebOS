# Repository-level acceptance: no legacy coupling in the shipped product, no forbidden
# artifacts, and a clean, self-contained package source.
$root = Get-RepoRoot
$map = [ordered]@{}

$map['repo: product, tools and docs contain no legacy Orbis coupling'] = {
    # scripts/home/ is the reviewed exception: its cleanup script must name the legacy
    # launcher package to remove the old prototype (see scripts/build/REPORT.md).
    $issues = @(Test-NoOrbisReferences)
    Assert-NoIssues $issues | Out-Null
    'no references (scripts/home/ reviewed exception excluded)'
}
$map['repo: shipped app/dock have no legacy launcher launch targets'] = {
    $issues = @(Test-NoLegacyLauncherCoupling -ScanRoots @('app', 'dock'))
    Assert-NoIssues $issues | Out-Null
    'no legacy launch targets in app/ or dock/'
}
$map['repo: no secrets, backups, IPKs or certificates tracked in the working tree'] = {
    $issues = @(Test-ForbiddenArtifacts)
    Assert-NoIssues $issues | Out-Null
    'clean'
}
$map['repo: app/ dev debris is excluded from the package (advisory)'] = {
    $appDir = Join-Path $root 'app'
    if (-not (Test-Path -LiteralPath $appDir)) { Block-Test 'app/ missing' }
    $junk = @(Get-ChildItem -LiteralPath $appDir -Recurse -Force -ErrorAction SilentlyContinue |
        Where-Object { $_.Name -match '(?i)^\.(edge|preview|chrome|cache)|__pycache__|\.pyc$' })
    if ($junk.Count -eq 0) { return 'no debris' }
    "WARN advisory (owner: Flash-high): $($junk.Count) local dev item(s) in app/, excluded from the IPK"
}
$map['repo: GPL/AGPL-family dependencies are documented if present'] = {
    # Proportionate licence screen: flag obvious copyleft package names in manifests/lockfiles.
    $hits = @()
    foreach ($f in (Get-ChildItem -LiteralPath $root -Recurse -File -Force -ErrorAction SilentlyContinue |
            Where-Object { $_.Name -match '^(package(-lock)?\.json|pnpm-lock\.yaml|yarn\.lock)$' -and $_.FullName -notmatch '(?i)[\\/](node_modules|\.git)[\\/]' })) {
        $t = Get-TextFileContent -Path $f.FullName
        if ($t -match '(?i)"(gpl-3\.0|agpl-3\.0)"') { $hits += (Get-RelativeRepoPath $f.FullName) }
    }
    if ($hits.Count -gt 0 -and -not (Test-Path -LiteralPath (Join-Path $root 'THIRD_PARTY_NOTICES.md'))) {
        throw ("copyleft dependency licence found without THIRD_PARTY_NOTICES.md: " + ($hits -join ', '))
    }
    if ($hits.Count -gt 0) { "notices present for: $($hits -join ', ')" } else { 'no copyleft manifests' }
}
$map['repo: CI wiring for the build/test entry points (advisory to Flash-off 1)'] = {
    $ci = Join-Path $root '.github\workflows\ci.yml'
    if (-not (Test-Path -LiteralPath $ci)) { Skip-Test '.github/workflows/ci.yml not present (Flash-off repo scope)' }
    $t = Get-TextFileContent -Path $ci
    $wanted = @(
        @{ Name = 'scripts/build/build.ps1'; Pattern = '(?i)scripts[\\/]build[\\/]build\.ps1|\./scripts/build/build\.ps1' },
        @{ Name = 'tests/lib/Run-Tests.ps1'; Pattern = '(?i)Run-Tests\.ps1|tests[\\/]lib' }
    )
    $missing = @($wanted | Where-Object { $t -notmatch $_.Pattern } | ForEach-Object { $_.Name })
    if ($missing.Count -gt 0) {
        return "WARN advisory (owner: Flash-off 1): CI does not invoke " + ($missing -join ', ') + "; recommend `"./scripts/build/build.ps1 -CheckOnly`" then `"./tests/lib/Run-Tests.ps1`""
    }
    'ci wired to build.ps1 and Run-Tests.ps1'
}

$map
