param([string]$Core = 'C:\Program Files\Synergy\synergy-core.exe')
$ErrorActionPreference = 'Stop'
if (-not (Test-Path -LiteralPath $Core -PathType Leaf)) { throw 'Core not found' }
if (Get-NetTCPConnection -LocalPort 24999 -State Listen -ErrorAction SilentlyContinue) { throw 'Port 24999 is occupied' }
$dir = Join-Path ([IO.Path]::GetTempPath()) ('synergy-isolated-' + [guid]::NewGuid())
New-Item -ItemType Directory -Path $dir | Out-Null
try {
 foreach ($range in @('0,0.0520833333','0,0.1','0,1','0,0')) {
  $config = @"
section: screens
 spicepc-fc6c0218:
 webos-tv:
end
section: links
 spicepc-fc6c0218:
  up($range) = webos-tv(0,100)
 webos-tv:
  down(0,100) = spicepc-fc6c0218($range)
end
section: options
 switchDelay = 0
end
"@
  $cfg=Join-Path $dir 'test.conf'; $out=Join-Path $dir 'stdout.txt'; $err=Join-Path $dir 'stderr.txt'
  [IO.File]::WriteAllText($cfg,$config,[Text.Encoding]::ASCII)
  $p=Start-Process -FilePath $Core -ArgumentList @('server','--address','127.0.0.1:24999','--config',('"'+$cfg+'"'),'--name','spicepc-fc6c0218','--no-restart','--no-tray','--debug','INFO') -PassThru -WindowStyle Hidden -RedirectStandardOutput $out -RedirectStandardError $err
  $exited=$p.WaitForExit(2500)
  if (-not $exited) { Stop-Process -Id $p.Id; $p.WaitForExit() }
  $logs=(Get-Content -LiteralPath $out,$err -Raw -ErrorAction SilentlyContinue) -join "`n"
  [pscustomobject]@{ Range=$range; ExitedEarly=$exited; ExitCode=if($exited){$p.ExitCode}else{'stopped-owned-test'}; Log=$logs.Trim() } | Format-List
 }
} finally { Remove-Item -LiteralPath $dir -Recurse -Force }
