$ErrorActionPreference = 'Stop'
$p = 'C:\Users\Administrator\AppData\Roaming\MouseLock\debug.log'
$pad = ('x' * 1024) * 1024        # 1 MB
for ($i = 0; $i -lt 3; $i++) { [System.IO.File]::AppendAllText($p, $pad) }
Write-Output ("填充后 debug.log 大小: " + (Get-Item $p).Length)
Start-Sleep -Seconds 4
Write-Output "--- 轮转后 ---"
Get-ChildItem 'C:\Users\Administrator\AppData\Roaming\MouseLock\debug.log*' |
    Select-Object Name, Length, LastWriteTime | Format-Table -AutoSize
Write-Output "--- 新文件头两行 ---"
Get-Content $p -TotalCount 2
