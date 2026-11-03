param([string]$tag = 'before', [int]$dur = 30, [int]$lockAt = 4, [int]$clicks = 12, [int]$overlayAt = 0)
$ErrorActionPreference = 'Stop'
$csv = "d:\nodejs\look\build\zprobe-$tag.csv"
if (Test-Path $csv) { Remove-Item $csv -Force }

Start-Process -FilePath 'd:\nodejs\look\build\zprobe.exe' -ArgumentList "$dur", "$csv" | Out-Null
Start-Sleep -Milliseconds 800
Write-Output "=== zoverlay: 锁定 +${lockAt}s / 遮罩 +${overlayAt}s / 总 $($dur-8)s ==="
& 'd:\nodejs\look\build\zoverlay.exe' ($dur - 8) $lockAt $clicks $overlayAt
Start-Sleep -Seconds 6
Write-Output "CSV: $csv"
