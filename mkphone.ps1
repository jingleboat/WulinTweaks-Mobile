# 一键：重打包 + zipalign + apksigner + 安装（手机端验证流水线）
# 用法： pwsh -File mkphone.ps1 [-Out E:\笔记\wulin_v3.apk] [-NoInstall] [-NoInject]
param(
    [string]$Src     = 'E:\笔记\base_sign.apk',
    [string]$So      = 'libil2cpp_intl_v3.so',
    [string]$Lib     = 'native\libwulin.so',
    [string]$Out     = 'E:\笔记\wulin_v3.apk',
    [switch]$NoInject,
    [switch]$NoInstall
)
$ErrorActionPreference = 'Stop'
$root = 'C:\Users\User\Documents\deepseek-harness\default-workspace\wulin-apk'
$bt   = 'C:\Users\User\andtools\bt\android-14'
$jdk  = 'C:\Users\User\andtools\jdk\jdk-17.0.20.1+1'
$adb  = 'C:\Users\User\pt\platform-tools\adb.exe'
Push-Location $root
try {
    $tmp = "$env:TEMP\wulin_raw.apk"
    $alg = "$env:TEMP\wulin_aligned.apk"
    Remove-Item $tmp, $alg -ErrorAction SilentlyContinue

    $args = @('mk_apk.py', 'build', '--src', $Src, '--out', $tmp, '--no-sign',
              '--replace', "lib/arm64-v8a/libil2cpp.so=$So")
    if (Test-Path $Lib) { $args += @('--add', "lib/arm64-v8a/libwulin.so=$Lib") }
    & python -X utf8 @args | Select-Object -Last 4

    & "$bt\zipalign.exe" -p -f 4 $tmp $alg
    if ($LASTEXITCODE) { throw 'zipalign failed' }

    $env:JAVA_HOME = $jdk
    $env:PATH = "$jdk\bin;$env:PATH"
    & "$bt\apksigner.bat" sign --ks keystore\wulin.jks --ks-pass pass:wulin123 `
        --key-pass pass:wulin123 --v1-signing-enabled true --v2-signing-enabled true `
        --v3-signing-enabled true --min-sdk-version 23 --out $Out $alg 2>&1 |
        Select-String -NotMatch 'WARNING' | Select-Object -Last 2
    if ($LASTEXITCODE) { throw 'apksigner failed' }
    Write-Host "signed -> $Out  ($([math]::Round((Get-Item $Out).Length/1MB,1)) MB)"

    if (-not $NoInstall) {
        & $adb install -r --no-incremental $Out 2>&1 | Select-Object -Last 1
        & $adb logcat -c
        & $adb shell monkey -p com.xd.dxlzz.global.gp -c android.intent.category.LAUNCHER 1 2>&1 |
            Select-Object -Last 1
    }
} finally {
    Remove-Item $tmp, $alg -ErrorAction SilentlyContinue
    Pop-Location
}
