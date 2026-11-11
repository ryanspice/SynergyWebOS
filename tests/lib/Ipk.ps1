# Minimal webOS IPK reader (ar archive with data.tar.gz / control.tar.gz members).
# Pure .NET: no tar/ar/gzip executables needed, so the artifact can be inspected on any
# host. Used by tests/integration to verify what the lead will actually install.

function Read-ArMembers {
    <# Parses a System V / GNU ar archive and returns member name -> byte[]. #>
    param([Parameter(Mandatory)][byte[]]$Bytes)
    $magic = [System.Text.Encoding]::ASCII.GetString($Bytes, 0, 8)
    if ($magic -notmatch '^!<arch>\n$') { throw "not an ar archive (magic '$magic')" }
    $members = @{}
    $pos = 8
    $gnuNames = ''
    while ($pos + 60 -le $Bytes.Length) {
        $header = [System.Text.Encoding]::ASCII.GetString($Bytes, $pos, 60)
        $name = $header.Substring(0, 16).Trim()
        $sizeText = $header.Substring(48, 10).Trim()
        # Trailer is the two bytes 0x60 0x0A ("`n").
        if ($header[58] -ne [char]0x60 -or $header[59] -ne [char]0x0A) { throw "bad ar header magic at offset $pos" }
        $size = [int]$sizeText
        $dataStart = $pos + 60
        if ($dataStart + $size -gt $Bytes.Length) { throw "truncated ar member '$name'" }
        $data = New-Object byte[] $size
        [System.Array]::Copy($Bytes, $dataStart, $data, 0, $size)
        if ($name -eq '//') { $gnuNames = [System.Text.Encoding]::ASCII.GetString($data) }
        elseif ($name -eq '/' -or $name -eq '/SYM64/') { }
        elseif ($name.StartsWith('/') -and $name.Substring(1) -match '^\d+$') {
            $idx = [int]$name.Substring(1)
            $end = $gnuNames.IndexOf("`n", $idx)
            if ($end -lt 0) { $end = $gnuNames.Length }
            $real = $gnuNames.Substring($idx, $end - $idx).TrimEnd('/')
            $members[$real] = $data
        } else {
            $members[$name.TrimEnd('/')] = $data
        }
        $pos = $dataStart + $size
        if ($size % 2 -eq 1) { $pos++ }
    }
    return $members
}

function Expand-TgzToMap {
    <# Decompresses a .tar.gz byte[] and returns relative path -> byte[] for regular files. #>
    param([Parameter(Mandatory)][byte[]]$Bytes)
    $files = @{}
    $ms = New-Object System.IO.MemoryStream(, $Bytes)
    try {
        $gz = New-Object System.IO.Compression.GZipStream($ms, [System.IO.Compression.CompressionMode]::Decompress)
        try {
            $tar = $gz
            $header = New-Object byte[] 512
            while ($true) {
                $read = 0
                while ($read -lt 512) {
                    $n = $tar.Read($header, $read, 512 - $read)
                    if ($n -le 0) { $read = 512; break }
                    $read += $n
                }
                $allZero = $true
                foreach ($b in $header) { if ($b -ne 0) { $allZero = $false; break } }
                if ($allZero) { break }
                $name = [System.Text.Encoding]::ASCII.GetString($header, 0, 100).Trim([char]0)
                $sizeField = [System.Text.Encoding]::ASCII.GetString($header, 124, 12).Trim([char]0, [char]32)
                $size = if ($sizeField) { [Convert]::ToInt64($sizeField, 8) } else { 0 }
                $type = [char]$header[156]
                $prefix = [System.Text.Encoding]::ASCII.GetString($header, 345, 155).Trim([char]0)
                # Some packagers write non-path bytes into the ustar prefix field; only join a
                # clean relative path, otherwise fall back to the 100-byte name field.
                $full = if ($prefix -match '^[A-Za-z0-9._/-]+$') { $prefix + '/' + $name } else { $name }
                $full = $full.TrimStart('./')
                $body = New-Object byte[] $size
                $got = 0
                while ($got -lt $size) {
                    $n = $tar.Read($body, $got, $size - $got)
                    if ($n -le 0) { break }
                    $got += $n
                }
                $pad = (512 - ($size % 512)) % 512
                $skip = New-Object byte[] $pad
                $skipped = 0
                while ($skipped -lt $pad) {
                    $n = $tar.Read($skip, $skipped, $pad - $skipped)
                    if ($n -le 0) { break }
                    $skipped += $n
                }
                if ($type -eq '0' -or $type -eq "`0" -or $type -eq '7') { $files[$full] = $body }
            }
        } finally { $gz.Dispose() }
    } finally { $ms.Dispose() }
    return $files
}

function Read-IpkFiles {
    <# Returns @{ AppFiles; ControlFiles } (relative path -> bytes) for a webOS IPK. #>
    param([Parameter(Mandatory)][string]$IpkPath)
    $bytes = [System.IO.File]::ReadAllBytes($IpkPath)
    $members = Read-ArMembers -Bytes $bytes
    $result = [pscustomobject]@{ AppFiles = @{}; ControlFiles = @{} }
    foreach ($k in $members.Keys) {
        if ($k -match '(?i)^data\.tar\.gz$') { $result.AppFiles = Expand-TgzToMap -Bytes $members[$k] }
        elseif ($k -match '(?i)^control\.tar\.gz$') { $result.ControlFiles = Expand-TgzToMap -Bytes $members[$k] }
    }
    if ($result.AppFiles.Count -eq 0) { throw 'IPK has no data.tar.gz payload' }
    return $result
}
