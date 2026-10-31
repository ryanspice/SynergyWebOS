$r = Invoke-RestMethod -Uri 'http://localhost:24803/v1/settings' -Method Get
if (-not $r.success) { throw 'Settings request failed' }
$d = $r.data
[pscustomobject]@{ServiceVersion=$d.serviceVersion; Override=$d.override.shouldOverride; ServerName=$d.override.serverName; SwitchDelay=$d.developers.switchDelay; RelativeMouse=$d.mouse.relative; Computers=($d.computers | ForEach-Object { $_.name }) -join ', ' } | Format-List
