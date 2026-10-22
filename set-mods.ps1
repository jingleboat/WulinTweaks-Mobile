# 修改国际版《大侠立志传》已启用 mod 列表（不需要重装 APK）
#
# 用法：
#   .\set-mods.ps1 -ListOnly                     # 列出 APK 里有哪些 mod
#   .\set-mods.ps1 -Mods "BaiBaoNang,BalanceMod" # 启用指定 mod
#   .\set-mods.ps1 -Mods all                     # 启用全部
#
# 原理：ModManager.TestLoadMods 把 "0|ModA|0|ModB" 写入全局键 ModsUsed，再据此加载。
# 本脚本改写 global-metadata.dat 里那条字符串字面量（追加到文件尾、只改字符串表的
# length/dataIndex），再推到 /sdcard/Android/data/<包名>/files/il2cpp/Metadata/。
param(
    [string]$Mods = "",
    [switch]$ListOnly,
    [string]$Package = "com.xd.dxlzz.global.gp",
    [string]$Serial = ""
)

$ErrorActionPreference = "Stop"
$here = Split-Path -Parent $MyInvocation.MyCommand.Path
$adb = "C:\Users\User\pt\platform-tools\adb.exe"
$orig = Join-Path $here "global-metadata_intl.dat"
$out = Join-Path $here "global-metadata_intl_mods.dat"

function Adb {
    if ($Serial) { & $adb -s $Serial @args } else { & $adb @args }
}

$modsAll = @(& python (Join-Path $here "list_mods.py"))

if ($ListOnly -or [string]::IsNullOrWhiteSpace($Mods)) {
    Write-Host "APK 内可用的 mod（共 $($modsAll.Count) 个）："
    $modsAll | ForEach-Object { "  $_" }
    if ([string]::IsNullOrWhiteSpace($Mods)) {
        Write-Host ""
        Write-Host '指定要启用的：.\set-mods.ps1 -Mods "BaiBaoNang,BalanceMod"'
    }
    return
}

if ($Mods -eq "all") { $Mods = ($modsAll -join ",") }

$bad = @($Mods.Split(",") | Where-Object { $modsAll -notcontains $_ })
if ($bad.Count -gt 0) { throw "这些 mod 不在 APK 里：$($bad -join ', ')" }

$n = ($Mods.Split(",")).Count
Write-Host "生成 metadata（$n 个 mod）..."
& python (Join-Path $here "meta_mods.py") --meta $orig --out $out --mods $Mods |
    Select-Object -Last 2

Write-Host "推送并重启游戏..."
Adb shell am force-stop $Package
Adb push $out "/sdcard/Android/data/$Package/files/il2cpp/Metadata/global-metadata.dat" |
    Select-Object -Last 1
Adb shell am start -n "$Package/com.unity3d.player.UnityPlayerActivity" | Out-Null
Write-Host "完成，等 20~30 秒进主菜单即可看到效果。"
