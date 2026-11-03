param([string]$key = 'IdleMode', [string]$value = '1')
$ErrorActionPreference = 'Stop'
$p = 'C:\Users\Administrator\AppData\Roaming\MouseLock\config.ini'

$text = [System.IO.File]::ReadAllText($p)   # 自动识别 BOM（该文件是 UTF-16LE）
if ($text -notmatch "(?m)^$key=\d+") { throw "未找到配置项 $key" }
$new = [regex]::Replace($text, "(?m)^$key=\d+", "$key=$value")
# 保持原编码：写回 UTF-16LE（带 BOM），否则 GetPrivateProfileIntW 仍能读，但和程序习惯不一致
[System.IO.File]::WriteAllText($p, $new, [System.Text.Encoding]::Unicode)
Write-Output "已写入 $key=$value"
Get-Content $p
