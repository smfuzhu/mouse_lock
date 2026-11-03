param([int]$h = 1040, [int]$dur = 10)
$ErrorActionPreference = 'Stop'
Write-Output "=== 启动高度 ${h}px 的置顶窗口（不锁定、不注入按键）==="
Start-Process -FilePath 'd:\nodejs\look\build\zoverlay.exe' -ArgumentList "$dur", '-1', '0', '0', "$h" | Out-Null
Start-Sleep -Seconds 4
Get-Content 'C:\Users\Administrator\AppData\Roaming\MouseLock\debug.log' -Tail 2
Start-Sleep -Seconds ($dur - 2)
Write-Output "=== 结束 ==="
Get-Content 'C:\Users\Administrator\AppData\Roaming\MouseLock\debug.log' -Tail 1
