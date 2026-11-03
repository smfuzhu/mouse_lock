# 用法: powershell -File build\zstat.ps1 [csv路径] [争夺起始ms]
$ErrorActionPreference = 'Stop'
$csvPath = if ($args.Count -ge 1 -and $args[0]) { $args[0] } else { 'd:\nodejs\look\build\zprobe-run.csv' }
$startOverride = if ($args.Count -ge 2 -and $args[1]) { [int]$args[1] } else { -1 }

$rows = Import-Csv $csvPath
Write-Output ("CSV: " + $csvPath)

$segs = New-Object System.Collections.ArrayList
$prev = $null
$start = 0
foreach ($r in $rows) {
    $c = $r.top_class
    $t = [int]$r.t_ms
    if ($null -eq $prev) { $prev = $c; $start = $t; continue }
    if ($c -ne $prev) {
        [void]$segs.Add([pscustomobject]@{ cls = $prev; start = $start; dur = ($t - $start) })
        $prev = $c
        $start = $t
    }
}
$last = [int]$rows[-1].t_ms
[void]$segs.Add([pscustomobject]@{ cls = $prev; start = $start; dur = ($last - $start) })

Write-Output ("总段数: " + $segs.Count)
Write-Output "---- 阶段（>=3000ms 的长段）----"
$segs | Where-Object { $_.dur -ge 3000 } | ForEach-Object {
    Write-Output ("  {0,-26} start={1,7} dur={2,7}" -f $_.cls, $_.start, $_.dur)
}
if ($segs.Count -le 20) {
    Write-Output "---- 全部段（段数少，直接列出）----"
    $segs | ForEach-Object {
        Write-Output ("  {0,-26} start={1,7} dur={2,7}" -f $_.cls, $_.start, $_.dur)
    }
}

$fightStart = $startOverride
if ($fightStart -lt 0) {
    for ($i = 1; $i -lt $segs.Count; $i++) {
        if ($segs[$i].dur -lt 200 -and $segs[$i - 1].dur -gt 2000) { $fightStart = $segs[$i].start; break }
    }
}

$fight = $segs | Where-Object { $_.start -ge $fightStart }
$cover = @($fight | Where-Object { $_.cls -eq 'MouseLockCoverClass' })
$ovl = @($fight | Where-Object { $_.cls -eq 'ResProbePrivacyOverlay' })
$span = ($last - $fightStart)

function Show-Stat($name, $arr) {
    $d = @($arr | ForEach-Object { $_.dur } | Sort-Object)
    if ($d.Count -eq 0) { Write-Output "$name : 空"; return }
    $sum = 0; foreach ($v in $d) { $sum += $v }
    $p = { param($q) $d[[math]::Min($d.Count - 1, [int][math]::Floor($q * $d.Count))] }
    Write-Output ("{0}: n={1} 合计={2}ms 占空={3:P1} 中位={4} p90={5} min={6} max={7}" -f `
            $name, $d.Count, $sum, ($sum / $span), (& $p 0.5), (& $p 0.9), $d[0], $d[-1])
}

Write-Output ("---- 争夺期(start={0}, 共 {1} ms) ----" -f $fightStart, $span)
Show-Stat 'MouseLockCoverClass   ' $cover
Show-Stat 'ResProbePrivacyOverlay' $ovl

$starts = @($cover | ForEach-Object { $_.start } | Sort-Object)
$diffs = @()
for ($i = 1; $i -lt $starts.Count; $i++) { $diffs += ($starts[$i] - $starts[$i - 1]) }
if ($diffs.Count -gt 0) {
    $sorted = @($diffs | Sort-Object)
    $sum = 0; foreach ($v in $sorted) { $sum += $v }
    Write-Output ("cover 露脸间隔: n={0} 平均={1:F1}ms 中位={2}ms min={3} max={4}" -f `
            $sorted.Count, ($sum / $sorted.Count), $sorted[[int]($sorted.Count / 2)], $sorted[0], $sorted[-1])
    Write-Output ("  落在 450~550ms: {0}/{1}" -f @($sorted | Where-Object { $_ -ge 450 -and $_ -le 550 }).Count, $sorted.Count)
}

# overlay 连续驻留 ~900ms = 中间一次 cover 露脸短于 60Hz 采样周期(16.7ms)，被漏采
$missed = @($fight | Where-Object { $_.cls -eq 'ResProbePrivacyOverlay' -and $_.dur -ge 700 })
$realFlashes = $cover.Count + $missed.Count
Write-Output ("疑似漏采（overlay 连续 >=700ms）: {0} 次 -> 推算真实露脸次数约 {1} 次，约 {2:F2} 次/秒" -f `
        $missed.Count, $realFlashes, ($realFlashes / ($span / 1000.0)))
$missed | ForEach-Object { Write-Output ("    start={0,7} dur={1,5}" -f $_.start, $_.dur) }
